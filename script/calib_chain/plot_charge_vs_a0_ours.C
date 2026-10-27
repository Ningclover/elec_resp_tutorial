// plot_charge_vs_a0_ours.C -- per-channel "A0 (ADC) vs injected charge Q (fC)"
// scatter + fitted straight line, for the VD pulser charge scan.
//
// This is the RAW data behind the slope_dAdQ plot: for a handful of channels it
// re-fits the pulse amplitude A0 (in ADC, true peak) at every scan run, plots
// A0 vs Q, and overlays the linear fit  A0 = intercept + slope_adc * Q.  The
// fitted slope_adc is the gain in ADC/fC (divide by ADC/mV=11.703 for mV/fC).
//
// Reuses the EXACT fit machinery from wf_gain_scan_ours.C (response_legacy,
// fit_channel, scan_points, magnify_path, adc_per_mV) by including it.
//
// Usage (inside container, dunesw sourced):
//   root -b -q 'plot_charge_vs_a0_ours.C("vd","daq")'      // default 6 channels
//   root -b -q 'plot_charge_vs_a0_ours.C("vd","raw")'
//   root -b -q 'plot_charge_vs_a0_ours.C("vd","both")'     // daq+raw overlaid
//   root -b -q 'plot_charge_vs_a0_ours.C("vd","both","100,1000,2000,3000,4000,5000")'
//
// Output: plots/charge_vs_a0_<det>_<variant>.png/.pdf

#include "wf_gain_scan_ours.C"   // brings in fit_channel, scan_points, magnify_path, adc_per_mV, g_tick_us

#include <TCanvas.h>
#include <TGraph.h>
#include <TGraphErrors.h>
#include <TF1.h>
#include <TLegend.h>
#include <TLatex.h>
#include <TStyle.h>
#include <TMultiGraph.h>
#include <sstream>

// VD pulse_dac per run (charge_fC = dac * 2.65105). Used when xaxis="dac".
static double vd_dac(int run) {
  switch (run) {
    case 41573: return 1;  case 41574: return 2;  case 41577: return 5;
    case 41582: return 10; case 41587: return 15; case 41593: return 21;
    case 41597: return 25; case 41602: return 30; case 41603: return 31;
    default: return -1;
  }
}

// global: if true, x-axis = pulse_dac (VD), else fC.
static bool g_xaxis_dac = false;

// collect (Q, A0) across the scan for ONE channel & variant.
// Q is in fC by default, or in DAC bits if g_xaxis_dac is set.
static int collect_channel(const TString& det, const TString& variant, int wantCh,
                           vector<double>& Q, vector<double>& A0) {
  Q.clear(); A0.clear();
  const char* h2name = (variant == "daq") ? "h_daq" : "h_raw";
  auto pts = scan_points(det);
  for (auto& sp : pts) {
    TString path = magnify_path(det, variant, sp);
    TFile* f = TFile::Open(path, "READ");
    if (!f || f->IsZombie()) { std::cerr << "  MISSING: " << path << endl; continue; }
    TH2* h2 = dynamic_cast<TH2*>(f->Get(h2name));
    if (!h2) { f->Close(); continue; }
    h2->SetDirectory(nullptr);
    static unsigned tag = 1000; h2->SetUniqueID(++tag);
    int bx = h2->GetXaxis()->FindBin(wantCh + 0.0001);
    int chHere = (int)(h2->GetXaxis()->GetBinCenter(bx) + 0.5);
    if (chHere != wantCh) { delete h2; f->Close(); continue; }
    double a=0, tp=0, c2=0;
    if (fit_channel(h2, bx, a, tp, c2) && a > 0 && tp > 0 && tp < 5) {
      double xval = sp.q;
      if (g_xaxis_dac && det == "vd") { xval = vd_dac(sp.run); if (xval < 0) { delete h2; f->Close(); continue; } }
      Q.push_back(xval); A0.push_back(a);
    }
    delete h2; f->Close();
  }
  return (int)Q.size();
}

void plot_charge_vs_a0_ours(const char* det_c, const char* variant_c = "both",
                            const char* chan_csv = "100,1100,2200,3300,4400,5500",
                            const char* xaxis = "fc") {
  TString det(det_c); det.ToLower();
  TString variant(variant_c); variant.ToLower();
  TString xax(xaxis); xax.ToLower();
  g_xaxis_dac = (xax == "dac");
  g_tick_us = 0.512;
  ROOT::Math::MinimizerOptions::SetDefaultMaxFunctionCalls(500000);
  gStyle->SetOptStat(0);
  gSystem->Exec("mkdir -p plots");

  // parse channel list
  vector<int> chans;
  std::stringstream ss(chan_csv); std::string tok;
  while (std::getline(ss, tok, ',')) if (!tok.empty()) chans.push_back(std::atoi(tok.c_str()));

  const double adcmv = adc_per_mV();
  bool doDaq = (variant == "daq" || variant == "both");
  bool doRaw = (variant == "raw" || variant == "both");

  int nc = (int)chans.size();
  int ncol = (nc <= 3) ? nc : 3;
  int nrow = (nc + ncol - 1) / ncol;
  TCanvas* c = new TCanvas("c_qa0", "A0 vs Q", 380*ncol, 320*nrow);
  c->Divide(ncol, nrow);

  TString detName = (det=="vd") ? "ProtoDUNE-VD" : "ProtoDUNE-HD";
  TString xunit = g_xaxis_dac ? "Injected charge (DAC)"
                              : ((det=="vd") ? "Injected charge Q (fC)" : "Injected charge Q (DAC)");
  TString slopeU = g_xaxis_dac ? "ADC/DAC" : "ADC/fC";

  for (int i = 0; i < nc; ++i) {
    c->cd(i+1);
    gPad->SetLeftMargin(0.16); gPad->SetBottomMargin(0.13);
    int ch = chans[i];

    TMultiGraph* mg = new TMultiGraph();
    mg->SetTitle(Form("%s  ch %d;%s;A_{0} (ADC, peak)", detName.Data(), ch, xunit.Data()));
    TLegend* leg = new TLegend(0.18, 0.72, 0.62, 0.88);
    leg->SetBorderSize(0); leg->SetFillStyle(0);

    auto draw_variant = [&](const TString& var, int color, const char* label) {
      vector<double> Q, A0;
      int n = collect_channel(det, var, ch, Q, A0);
      if (n < 2) { cout << "  ch " << ch << " " << var << ": only " << n << " pts\n"; return; }
      TGraph* g = new TGraph(n, &Q[0], &A0[0]);
      g->SetMarkerStyle(20); g->SetMarkerSize(1.1);
      g->SetMarkerColor(color); g->SetLineColor(color);
      TF1* line = new TF1(Form("ln_%d_%s", ch, var.Data()), "[0]+[1]*x", 0, 1e9);
      line->SetParameters(0, A0.back()/std::max(Q.back(),1e-9));
      TFitResultPtr r = g->Fit(line, "SQN");
      double inter = r->Parameter(0), sl = r->Parameter(1);   // ADC/fC
      line->SetParameters(inter, sl);
      line->SetLineColor(color); line->SetLineStyle(2); line->SetLineWidth(2);
      line->SetRange(0, 1.05*Q.back());
      mg->Add(g, "P");
      g->GetListOfFunctions()->Add(line);   // so the line draws with the graph
      if (g_xaxis_dac)
        leg->AddEntry(g, Form("%s: %.1f ADC/DAC", label, sl), "p");
      else
        leg->AddEntry(g, Form("%s: %.1f ADC/fC = %.2f mV/fC", label, sl, sl/adcmv), "p");
      cout << "  ch " << ch << " " << var << ": n=" << n
           << "  slope=" << sl << " " << slopeU
           << (g_xaxis_dac ? "" : Form("  (%.3f mV/fC)", sl/adcmv))
           << "  inter=" << inter << endl;
    };

    if (doDaq) draw_variant("daq", kRed,  "Without decon");
    if (doRaw) draw_variant("raw", kBlue, "With decon");

    mg->Draw("AP");
    mg->GetXaxis()->SetLimits(0, g_xaxis_dac ? 34 : 90);
    leg->Draw();
  }

  TString outv = (variant=="both") ? "both" : variant;
  TString xtag = g_xaxis_dac ? "_dac" : "";
  c->SaveAs(Form("plots/charge_vs_a0_%s_%s%s.png", det.Data(), outv.Data(), xtag.Data()));
  c->SaveAs(Form("plots/charge_vs_a0_%s_%s%s.pdf", det.Data(), outv.Data(), xtag.Data()));
  cout << "[charge_vs_a0] wrote plots/charge_vs_a0_" << det << "_" << outv << xtag << ".png/.pdf\n";
}
