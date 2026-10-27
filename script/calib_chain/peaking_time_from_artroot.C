// peaking_time_from_artroot.C
//
// Single-run peaking-time validation plot for PDVD, built directly from the
// reprocessed eleccalib art-ROOT (gallery), overlaying the per-channel design
// shaping time stored in the chresp JSON.
//
//   RED   "Without Deconvolution" : fit the ORIG pulse (tpcrawdecoder:daq)
//   BLUE  "With Deconvolution"     : fit the RAW  pulse (wclsdatavdfilter:raw)
//   GREEN "Config shaping (JSON)"  : per-channel shaping/1000 us from the
//                                    protodunevd-params-channel-response-*.json.bz2
//
// Both pulses come from the SAME reprocessed file, so the orig->raw shift is the
// pure effect of the per-channel electronics-response correction.  Each channel
// is fit with the IDEAL CR-RC^4 cold-electronics response (response_legacy),
// identical to wf_gain_scan.C / wf_fitter_ideal.C, and the fitted t_p is binned.
//
// Usage (inside container, dunesw sourced):
//   root -b -q 'peaking_time_from_artroot.C("<eleccalib_h5.root>","<chresp.json>",41602)'
// The chresp path may be a decompressed .json (pass the path to a json you
// bzcat'd out first); see run wrapper below.

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <string>
#include <vector>

#include "lardataobj/Utilities/sparse_vector.h"
#include "canvas/Utilities/InputTag.h"
#include "gallery/Event.h"
#include "gallery/Handle.h"
#include "lardataobj/RawData/RawDigit.h"

#include "TCanvas.h"
#include "TF1.h"
#include "TFile.h"
#include "TFitResult.h"
#include "TFitResultPtr.h"
#include "TH1D.h"
#include "TH1F.h"
#include "TLegend.h"
#include "TMath.h"
#include "TStyle.h"
#include "Math/MinimizerOptions.h"

using namespace art;
using std::cout;
using std::endl;
using std::vector;

static double g_tick_us = 0.512;  // 512 ns/tick native VD grid

// ---- ideal CR-RC^4 cold-electronics response (par: t0, A0, tp) -------------
static double response_legacy(double* x, double* par) {
  double t = x[0]-par[0], A0=par[1], tp=par[2], rt=t/tp, g=A0*1.012;
  double v =
      4.31054*exp(-2.94809*rt)*g
    - 2.6202 *exp(-2.82833*rt)*cos(1.19361*rt)*g
    - 2.6202 *exp(-2.82833*rt)*cos(1.19361*rt)*cos(2.38722*rt)*g
    + 0.464924*exp(-2.40318*rt)*cos(2.5928*rt)*g
    + 0.464924*exp(-2.40318*rt)*cos(2.5928*rt)*cos(5.18561*rt)*g
    + 0.762456*exp(-2.82833*rt)*sin(1.19361*rt)*g
    - 0.762456*exp(-2.82833*rt)*cos(2.38722*rt)*sin(1.19361*rt)*g
    + 0.762456*exp(-2.82833*rt)*cos(1.19361*rt)*sin(2.38722*rt)*g
    - 2.620200*exp(-2.82833*rt)*sin(1.19361*rt)*sin(2.38722*rt)*g
    - 0.327684*exp(-2.40318*rt)*sin(2.5928*rt)*g
    + 0.327684*exp(-2.40318*rt)*cos(5.18561*rt)*sin(2.5928*rt)*g
    - 0.327684*exp(-2.40318*rt)*cos(2.5928*rt)*sin(5.18561*rt)*g
    + 0.464924*exp(-2.40318*rt)*sin(2.5928*rt)*sin(5.18561*rt)*g;
  if (t > 0 && t < 10) return v;
  return 0;
}

// ---- fit one channel's waveform; returns fitted tp (us), <=0 on failure -----
// adc: pedestal-subtracted samples (one event).
static double fit_tp(const vector<double>& adc) {
  int nt = (int)adc.size();
  if (nt < 50) return -1;

  // locate first prominent positive peak (>= 50% of global max)
  double mx = -1e30; int mxbin = -1;
  for (int i = 0; i < nt; ++i) if (adc[i] > mx) { mx = adc[i]; mxbin = i; }
  if (mx <= 0) return -1;
  double thr = 0.5 * mx;
  int pk = mxbin;
  for (int i = 1; i < nt-1; ++i)
    if (adc[i] >= thr && adc[i] >= adc[i-1] && adc[i] >= adc[i+1]) { pk = i; break; }

  // window [pk-15, pk+100] like wf_gain_scan.C, x-axis in microseconds
  int nbefore = 15, nafter = 100;
  int lo = std::max(0, pk - nbefore);
  int hi = std::min(nt - 1, pk + nafter);
  int nb = hi - lo + 1;
  if (nb < 20) return -1;

  TH1F hp("hp_tp", "", nb, 0, nb * g_tick_us);
  double sum = 0;
  for (int k = lo; k <= hi; ++k) {
    double c = adc[k];
    if (std::isnan(c)) return -1;
    hp.SetBinContent(k - lo + 1, c);
    sum += c;
  }
  if (std::fabs(sum) < 1e-10) return -1;

  TF1 f("f_tp", response_legacy, 0, 100, 3);
  f.SetNpx(1000);
  f.SetParameters(4.5, hp.GetMaximum(), 2.0);
  TFitResultPtr r = hp.Fit(&f, "RSQN", "", 0, 50.0);
  if (r.Get() == nullptr) return -1;
  double tp = r->Parameter(2);
  if (tp <= 0 || tp > 5) return -1;
  return tp;
}

// ---- read a RawDigit collection by trying candidate tags -------------------
static const vector<raw::RawDigit>*
get_digits(gallery::Event& ev, const vector<InputTag>& tags,
           gallery::Handle<vector<raw::RawDigit>>& keep, std::string& used) {
  for (auto const& t : tags) {
    gallery::Handle<vector<raw::RawDigit>> h;
    if (ev.getByLabel(t, h) && h.isValid() && !h->empty()) {
      keep = h; used = t.encode(); return &(*h);
    }
  }
  return nullptr;
}

void peaking_time_from_artroot(std::string const& artroot,
                               std::string const& json_shaping_txt,
                               int run = 41602) {
  gStyle->SetOptStat(0);
  g_tick_us = 0.512;
  ROOT::Math::MinimizerOptions::SetDefaultMaxFunctionCalls(500000);

  vector<InputTag> orig_tags{
    InputTag{"tpcrawdecoder:daq"},
    InputTag{"tpcrawdecoder:daq:pdvdeleccalibh5"},
  };
  vector<InputTag> raw_tags{
    InputTag{"wclsdatavdfilter:raw:pdvdeleccalibh5"},
    InputTag{"wclsdatavdfilter:raw:pdvdeleccalib"},
    InputTag{"wclsdatavdfilter:raw:pdvdkeepupstage1"},
  };

  vector<std::string> files(1, artroot);

  TString detName = "ProtoDUNE-VD";
  double tLo = 1.0, tHi = 4.0; int tN = 90;
  TH1F* htd = new TH1F("htd", Form("%s;Peaking time t_{p} (#mus);Channel Count", detName.Data()), tN, tLo, tHi);
  TH1F* htr = new TH1F("htr", "", tN, tLo, tHi);
  TH1F* hjs = new TH1F("hjs", "", tN, tLo, tHi);

  // ---------- fit orig + raw from the art-ROOT ----------
  for (gallery::Event ev(files); !ev.atEnd(); ev.next()) {
    auto aux = ev.eventAuxiliary();
    cout << "processing event " << aux.run() << "-" << aux.event() << endl;

    gallery::Handle<vector<raw::RawDigit>> ho, hr;
    std::string otag, rtag;
    auto* orig = get_digits(ev, orig_tags, ho, otag);
    auto* rawc = get_digits(ev, raw_tags,  hr, rtag);
    if (!orig) { cout << "ERROR: no orig digits\n"; return; }
    if (!rawc) { cout << "ERROR: no raw digits\n";  return; }
    cout << "  orig: " << otag << " (" << orig->size() << ")\n";
    cout << "  raw : " << rtag << " (" << rawc->size() << ")\n";

    int nO = 0, nR = 0;
    vector<double> buf;
    for (auto const& rd : *orig) {
      int ns = rd.Samples(); double ped = rd.GetPedestal();
      buf.assign(ns, 0);
      for (int j = 0; j < ns; ++j) buf[j] = rd.ADC(j) - ped;
      double tp = fit_tp(buf);
      if (tp > 0) { htd->Fill(tp); nO++; }
    }
    for (auto const& rd : *rawc) {
      int ns = rd.Samples(); double ped = rd.GetPedestal();
      buf.assign(ns, 0);
      for (int j = 0; j < ns; ++j) buf[j] = rd.ADC(j) - ped;
      double tp = fit_tp(buf);
      if (tp > 0) { htr->Fill(tp); nR++; }
    }
    cout << "  orig fits: " << nO << "  raw fits: " << nR << endl;
    break;  // first event only
  }

  // ---------- JSON config shaping ----------
  // json_shaping_txt is a plain text file: one shaping value (ns) per channel,
  // produced by the wrapper (bzcat | python).
  {
    std::ifstream ifs(json_shaping_txt);
    double sh_ns; int nJ = 0;
    while (ifs >> sh_ns) { hjs->Fill(sh_ns / 1000.0); nJ++; }
    cout << "  JSON shaping entries: " << nJ << endl;
  }

  // ---------- draw ----------
  htd->SetLineColor(kRed);   htd->SetLineWidth(2);
  htr->SetLineColor(kBlue);  htr->SetLineWidth(2);
  hjs->SetLineColor(kGreen+2); hjs->SetLineWidth(2); hjs->SetLineStyle(2);

  TCanvas* c = new TCanvas("c_tp", "tp", 800, 600);
  double m = std::max({htd->GetMaximum(), htr->GetMaximum(), hjs->GetMaximum()});
  htd->SetMaximum(1.18 * m);
  htd->Draw("HIST"); htr->Draw("HIST SAME"); hjs->Draw("HIST SAME");

  TLegend* l = new TLegend(0.50, 0.72, 0.88, 0.88);
  l->AddEntry(htd, "Without Deconvolution (orig)", "l");
  l->AddEntry(htr, "With Deconvolution (raw)", "l");
  l->AddEntry(hjs, "Config shaping (JSON 41602)", "l");
  l->Draw();

  c->SaveAs(Form("plots/peaking_time_vd_41602.png", run));
  c->SaveAs(Form("plots/peaking_time_vd_41602.pdf", run));

  cout << "\n========  " << detName << "  (run " << run << ")  ========\n";
  cout << "  orig  (no decon)  : mean tp = " << htd->GetMean() << "  RMS = " << htd->GetRMS() << "  N = " << htd->GetEntries() << "\n";
  cout << "  raw   (decon)     : mean tp = " << htr->GetMean() << "  RMS = " << htr->GetRMS() << "  N = " << htr->GetEntries() << "\n";
  cout << "  JSON  shaping     : mean tp = " << hjs->GetMean() << "  RMS = " << hjs->GetRMS() << "  N = " << hjs->GetEntries() << "\n";
  cout << "  wrote plots/peaking_time_vd_41602.png\n";
}
