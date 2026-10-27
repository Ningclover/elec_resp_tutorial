// vd_orig_vs_raw_compare.C
//
// PDVD electronics-response calibration: 1D waveform comparison between the
// UNCORRECTED before-image ("orig", read from tpcrawdecoder:daq) and the
// per-channel CORRECTED RawDigits ("raw", written by the calibration NF as
// wclsdatavdfilter:raw:pdvdeleccalibh5).
//
// Both products live in the *_eleccalib_h5.root art-ROOT (the NF job keeps the
// decoder output via outputCommands: "keep *").  For each of N randomly chosen
// channels this draws the orig and raw ADC-minus-pedestal waveform overlaid on
// one pad, writes a multi-page PDF and a ROOT file of the per-channel TH1s.
//
// Usage (inside container, after dunesw setup):
//   root -b -q 'vd_orig_vs_raw_compare.C("output/..._eleccalib_h5.root", 41602, 100, 12345)'
//
// args: filename, run, nchannels-to-draw (default 100), RNG seed (default 12345)

#include <algorithm>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "lardataobj/Utilities/sparse_vector.h"
#include "canvas/Utilities/InputTag.h"
#include "gallery/Event.h"
#include "gallery/Handle.h"

#include "lardataobj/RawData/RawDigit.h"

#include "TCanvas.h"
#include "TFile.h"
#include "TH1D.h"
#include "TLegend.h"
#include "TRandom3.h"
#include "TStyle.h"

using namespace art;
using namespace std;

void
vd_orig_vs_raw_compare(std::string const& filename,
                       int run = 41602,
                       int ndraw = 100,
                       unsigned int seed = 12345)
{
  // Corrected ("raw") product: try the from-HDF5 calib process first, then the
  // reco_stage1 calib process, then the upstream keepup tag.
  std::vector<InputTag> raw_tags{
    InputTag{ "wclsdatavdfilter:raw:pdvdeleccalibh5" },
    InputTag{ "wclsdatavdfilter:raw:pdvdeleccalib" },
    InputTag{ "wclsdatavdfilter:raw:pdvdkeepupstage1" },
  };
  // Uncorrected before-image ("orig"): the decoder output.
  std::vector<InputTag> orig_tags{
    InputTag{ "tpcrawdecoder:daq" },
    InputTag{ "tpcrawdecoder:daq:pdvdeleccalibh5" },
  };

  vector<string> filenames(1, filename);

  for (gallery::Event ev(filenames); !ev.atEnd(); ev.next()) {
    auto aux = ev.eventAuxiliary();
    std::cout << "processing event: " << aux.run() << "-" << aux.event() << std::endl;

    // ---- locate corrected RawDigits ---------------------------------------
    gallery::Handle<vector<raw::RawDigit>> raw_h;
    InputTag raw_tag;
    bool found_raw = false;
    for (auto const& t : raw_tags) {
      gallery::Handle<vector<raw::RawDigit>> h;
      if (ev.getByLabel(t, h) && h.isValid() && !h->empty()) {
        raw_h = h; raw_tag = t; found_raw = true; break;
      }
    }
    if (!found_raw) { std::cout << "ERROR: no corrected (raw) RawDigits found.\n"; return; }

    // ---- locate uncorrected RawDigits -------------------------------------
    gallery::Handle<vector<raw::RawDigit>> orig_h;
    InputTag orig_tag;
    bool found_orig = false;
    for (auto const& t : orig_tags) {
      gallery::Handle<vector<raw::RawDigit>> h;
      if (ev.getByLabel(t, h) && h.isValid() && !h->empty()) {
        orig_h = h; orig_tag = t; found_orig = true; break;
      }
    }
    if (!found_orig) { std::cout << "ERROR: no uncorrected (orig) RawDigits found.\n"; return; }

    std::cout << "  orig tag: " << orig_tag.encode() << "  (" << orig_h->size() << " digits)\n";
    std::cout << "  raw  tag: " << raw_tag.encode()  << "  (" << raw_h->size()  << " digits)\n";

    // ---- index by channel -------------------------------------------------
    std::map<int, raw::RawDigit const*> orig_by_ch, raw_by_ch;
    for (auto const& rd : *orig_h) orig_by_ch[rd.Channel()] = &rd;
    for (auto const& rd : *raw_h)  raw_by_ch[rd.Channel()]  = &rd;

    // channels present in BOTH
    std::vector<int> common;
    for (auto const& kv : raw_by_ch)
      if (orig_by_ch.count(kv.first)) common.push_back(kv.first);
    std::sort(common.begin(), common.end());
    std::cout << "  channels in both products: " << common.size() << std::endl;
    if (common.empty()) { std::cout << "ERROR: no common channels.\n"; return; }

    // ---- choose N random channels -----------------------------------------
    TRandom3 rng(seed);
    int n = std::min<int>(ndraw, (int)common.size());
    std::set<int> picked_idx;
    while ((int)picked_idx.size() < n)
      picked_idx.insert((int)(rng.Uniform(0, common.size())));
    std::vector<int> channels;
    for (int idx : picked_idx) channels.push_back(common[idx]);
    std::sort(channels.begin(), channels.end());

    std::cout << "  drawing " << channels.size() << " channels (seed=" << seed << ")\n";

    // ---- draw -------------------------------------------------------------
    gStyle->SetOptStat(0);
    TCanvas c("c", "orig vs raw", 1000, 600);

    std::string pdf = Form("vd_orig_vs_raw_compare_%d.pdf", run);
    std::string rootf = Form("vd_orig_vs_raw_compare_%d.root", run);
    auto* ofile = TFile::Open(rootf.c_str(), "recreate");

    c.Print((pdf + "[").c_str());  // open multipage

    for (size_t i = 0; i < channels.size(); ++i) {
      int ch = channels[i];
      raw::RawDigit const& ord = *orig_by_ch[ch];
      raw::RawDigit const& rrd = *raw_by_ch[ch];

      double oped = ord.GetPedestal();
      double rped = rrd.GetPedestal();
      int nt = std::min(ord.Samples(), rrd.Samples());

      auto* ho = new TH1D(Form("orig_ch%d", ch),
                          Form("PDVD run %d  channel %d;Tick;ADC - pedestal", run, ch),
                          nt, 0, nt);
      auto* hr = new TH1D(Form("raw_ch%d", ch), "", nt, 0, nt);

      for (int j = 0; j < nt; ++j) {
        ho->SetBinContent(j + 1, ord.ADC(j) - oped);
        hr->SetBinContent(j + 1, rrd.ADC(j) - rped);
      }

      ho->SetLineColor(kBlack);
      ho->SetLineWidth(1);
      hr->SetLineColor(kRed);
      hr->SetLineWidth(1);

      // ---- find the FIRST positive peak (orig) and zoom around it ----------
      // Locate the bin of the first prominent positive excursion in the orig
      // waveform, then window the x-axis to a small region around it so the
      // pulse shape (and the orig->raw reshaping/shift) is visible.
      int peakbin = ho->GetMaximumBin();           // global max as a robust anchor
      double thresh = 0.3 * ho->GetMaximum();       // 30% of the tallest pulse
      for (int b = 1; b <= nt; ++b) {
        if (ho->GetBinContent(b) > thresh) { peakbin = b; break; }
      }
      int half = 150;                               // ticks on each side of the peak
      int lo = std::max(1, peakbin - half);
      int hi = std::min(nt, peakbin + half);
      ho->GetXaxis()->SetRange(lo, hi);

      // y-range from BOTH histos within the zoom window
      double ymin = 1e30, ymax = -1e30;
      for (int b = lo; b <= hi; ++b) {
        double vo = ho->GetBinContent(b), vr = hr->GetBinContent(b);
        ymin = std::min(ymin, std::min(vo, vr));
        ymax = std::max(ymax, std::max(vo, vr));
      }
      double pad = 0.1 * (ymax - ymin + 1.0);
      ho->SetMinimum(ymin - pad);
      ho->SetMaximum(ymax + pad);

      ho->Draw("hist");
      hr->Draw("hist same");

      TLegend leg(0.65, 0.78, 0.88, 0.88);
      leg.AddEntry(ho, "orig (tpcrawdecoder)", "l");
      leg.AddEntry(hr, "raw (perChannelShaper)", "l");
      leg.Draw();

      c.Print(pdf.c_str());

      ofile->cd();
      ho->Write();
      hr->Write();
    }

    c.Print((pdf + "]").c_str());  // close multipage
    ofile->Close();

    std::cout << "wrote " << pdf << " and " << rootf << std::endl;
    return;  // first event only
  }
}
