// plot_gain_tp.C -- reproduce the calibration validation plots:
//   (1) per-channel slope dA/dQ  (gain)         -- red "Without Deconvolution"
//   (2) per-channel peaking time t_p (us)        vs blue "With Deconvolution"
// from the gainscan_<det>_<variant>.root trees made by wf_gain_scan.C, for one
// detector.  Also prints the mean gain and mean t_p before/after (like the slide).
//
// Usage (inside container, dunesw sourced):
//   root -b -q 'plot_gain_tp.C("vd")'
//   root -b -q 'plot_gain_tp.C("hd")'
//
// VD slope is in ADC/fC (charge axis in fC).  HD slope is in ADC/DAC (charge
// axis = DAC); multiply by the HD fC-per-DAC scalar to get ADC/fC later.

#include <TFile.h>
#include <TTree.h>
#include <TH1F.h>
#include <TCanvas.h>
#include <TLegend.h>
#include <TStyle.h>
#include <TString.h>
#include <TMath.h>
#include <iostream>

using std::cout; using std::endl;

struct Stats { double mean, rms, n; };

static Stats fill_from_tree(const char* path, const char* branch, TH1F* h,
                            double lo, double hi) {
  TFile* f = TFile::Open(path, "READ");
  if (!f || f->IsZombie()) { std::cerr << "cannot open " << path << endl; return {0,0,0}; }
  TTree* t = (TTree*)f->Get("gain");
  if (!t) { std::cerr << "no 'gain' tree in " << path << endl; f->Close(); return {0,0,0}; }
  double v=0; t->SetBranchAddress(branch, &v);
  double sum=0, sum2=0; long n=0;
  for (Long64_t i=0;i<t->GetEntries();++i) {
    t->GetEntry(i);
    if (!TMath::Finite(v)) continue;
    if (v < lo || v > hi) { if (h) h->Fill(v < lo ? lo : hi-1e-9); }  // pile into edge
    else if (h) h->Fill(v);
    if (v>=lo && v<=hi) { sum+=v; sum2+=v*v; n++; }
  }
  f->Close();
  double mean = n? sum/n : 0;
  double rms  = n? sqrt(std::max(0.0, sum2/n - mean*mean)) : 0;
  return {mean, rms, (double)n};
}

void plot_gain_tp_ours(const char* det_c) {
  TString det(det_c); det.ToLower();
  gStyle->SetOptStat(0);
  TString base = "/exp/dune/data/users/xning/proto-dune-vd/elec_res/root_files";
  TString fdaq = Form("%s/gainscan_%s_ours_daq.root", base.Data(), det.Data());
  TString fraw = Form("%s/gainscan_%s_ours_raw.root", base.Data(), det.Data());

  TString detName = (det=="vd") ? "ProtoDUNE-VD" : "ProtoDUNE-HD";
  // VD slope branch is now PHYSICAL mV/fC (wf_gain_scan_ours.C applies the *10
  // template fix + divides by the derived ADC/mV). HD slope is still ADC/DAC.
  TString slopeUnit = (det=="vd") ? "mV/fC" : "ADC/DAC";

  // --- axis ranges (from actual data distributions) ---
  double gLo, gHi; int gN = 120;
  double tLo = 1.6, tHi = 2.5; int tN = 90;
  if (det=="vd") { gLo = 6;  gHi = 10; }      // mV/fC  (VD nominal ~7.8 mV/fC), zoomed
  else           { gLo = 0;  gHi = 3500; }    // ADC/DAC (HD)

  // ---------- (1) slope dA/dQ ----------
  TH1F* hgd = new TH1F("hgd", Form("%s;Slope dA/dQ (%s);Channel Count", detName.Data(), slopeUnit.Data()), gN, gLo, gHi);
  TH1F* hgr = new TH1F("hgr", "", gN, gLo, gHi);
  Stats sgd = fill_from_tree(fdaq, "slope", hgd, gLo, gHi);
  Stats sgr = fill_from_tree(fraw, "slope", hgr, gLo, gHi);
  hgd->SetLineColor(kRed);  hgd->SetLineWidth(2);
  hgr->SetLineColor(kBlue); hgr->SetLineWidth(2);

  TCanvas* c1 = new TCanvas("c_slope", "slope", 700, 550);
  double m1 = std::max(hgd->GetMaximum(), hgr->GetMaximum());
  hgd->SetMaximum(1.15*m1); hgd->Draw("HIST"); hgr->Draw("HIST SAME");
  TLegend* l1 = new TLegend(0.55,0.75,0.88,0.88);
  l1->AddEntry(hgd,"Without Deconvolution","l");
  l1->AddEntry(hgr,"With Deconvolution","l");
  l1->Draw();
  c1->SaveAs(Form("plots/slope_dAdQ_%s_ours.png", det.Data()));
  c1->SaveAs(Form("plots/slope_dAdQ_%s_ours.pdf", det.Data()));

  // ---------- (2) peaking time t_p ----------
  TH1F* htd = new TH1F("htd", Form("%s;Peaking time t_{p} (#mus);Channel Count", detName.Data()), tN, tLo, tHi);
  TH1F* htr = new TH1F("htr", "", tN, tLo, tHi);
  Stats std_ = fill_from_tree(fdaq, "tp_ref", htd, tLo, tHi);
  Stats str_ = fill_from_tree(fraw, "tp_ref", htr, tLo, tHi);
  htd->SetLineColor(kRed);  htd->SetLineWidth(2);
  htr->SetLineColor(kBlue); htr->SetLineWidth(2);

  TCanvas* c2 = new TCanvas("c_tp", "tp", 700, 550);
  double m2 = std::max(htd->GetMaximum(), htr->GetMaximum());
  htd->SetMaximum(1.15*m2); htd->Draw("HIST"); htr->Draw("HIST SAME");
  TLegend* l2 = new TLegend(0.55,0.75,0.88,0.88);
  l2->AddEntry(htd,"Without Deconvolution","l");
  l2->AddEntry(htr,"With Deconvolution","l");
  l2->Draw();
  c2->SaveAs(Form("plots/peaking_time_%s_ours.png", det.Data()));
  c2->SaveAs(Form("plots/peaking_time_%s_ours.pdf", det.Data()));

  // ---------- summary numbers (slide-style) ----------
  cout << "\n================  " << detName << "  ================\n";
  cout << "Slope dA/dQ (" << slopeUnit << "):\n";
  cout << "  Without Deconvolution : mean=" << sgd.mean << "  RMS=" << sgd.rms << "  N=" << sgd.n << "\n";
  cout << "  With    Deconvolution : mean=" << sgr.mean << "  RMS=" << sgr.rms << "  N=" << sgr.n << "\n";
  cout << "Peaking time t_p (us):\n";
  cout << "  Without Deconvolution : mean=" << std_.mean << "  RMS=" << std_.rms << "  N=" << std_.n << "\n";
  cout << "  With    Deconvolution : mean=" << str_.mean << "  RMS=" << str_.rms << "  N=" << str_.n << "\n";
  cout << "plots -> plots/slope_dAdQ_" << det << "_ours.png , plots/peaking_time_" << det << "_ours.png\n";
}
