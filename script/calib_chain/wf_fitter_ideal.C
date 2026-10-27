// Ideal cold-electronics response fitter for ELECTRONICS-RESPONSE CALIBRATION.
//
// Reads a magnify TH2 ("h_raw") produced from the per-channel response-equalized
// RawDigits (after the calibration noise filter), finds the signal pulse(s) on
// each channel, and fits each pulse with the IDEAL CR-RC^4 cold-electronics
// response (response_legacy, 3 params: t, A_0, t_p).  Writes per-channel fit
// results (t_p, A_0, chi2/dof) to txt + ROOT.
//
// If the per-channel correction worked, every channel should fit the ideal
// response well (chi2/dof ~ 1) with a consistent peaking time t_p.
//
// This is a generalized, after-correction-only distillation of
// wf_fitter_VD_DECON.C (Karla Tellez).
//
// Usage (inside the container, after sourcing dunesw so ROOT is available):
//   root -b -q 'wf_fitter_ideal.C("hraw-pdvd-magnify-41573-4.root", "vd", 41573)'
//   root -b -q 'wf_fitter_ideal.C("hraw-magnify-30413-8.root",      "hd", 30413)'

#include <TFile.h>
#include <TTree.h>
#include <TH1D.h>
#include <TH2I.h>
#include <TF1.h>
#include <TMath.h>
#include <TArrayD.h>
#include <TAxis.h>
#include <TString.h>
#include "TFitResult.h"
#include "TFitResultPtr.h"
#include "Math/MinimizerOptions.h"

#include <iostream>
#include <fstream>
#include <iomanip>
#include <vector>
#include <limits>
#include <cmath>

using std::cout;
using std::endl;

// ---- tick -> microsecond scale (detector-dependent sampling) ----
static double g_tick_us = 0.512;  // set in fitter() from detector
static double ScaleX(double x) { return g_tick_us * x; }

static void ScaleAxis(TAxis* a, double (*Scale)(double)) {
  if (!a) return;
  if (a->GetXbins()->GetSize()) {
    TArrayD X(*(a->GetXbins()));
    for (int i = 0; i < X.GetSize(); i++) X[i] = Scale(X[i]);
    a->Set((X.GetSize() - 1), X.GetArray());
  } else {
    a->Set(a->GetNbins(), Scale(a->GetXmin()), Scale(a->GetXmax()));
  }
}
static void ScaleXaxis(TH1* h, double (*Scale)(double)) {
  if (!h) return;
  ScaleAxis(h->GetXaxis(), Scale);
}

// ---- peak finder (positive pulses + following undershoot) ----
static void FindPeaks(TH1D* hist, double positiveThreshold, double negativeThreshold,
                      std::vector<int>& positivePeakBins, std::vector<double>& positivePeakValues,
                      std::vector<int>& negativePeakBins, std::vector<double>& negativePeakValues) {
  positivePeakBins.clear(); positivePeakValues.clear();
  negativePeakBins.clear(); negativePeakValues.clear();

  bool inPositivePeakRegion = false;
  bool foundPositivePeak = false;
  double maxNegativePeakValue = std::numeric_limits<double>::max();
  int maxNegativePeakBin = -1;
  bool inNegativePeak = false;
  int startMaxBin = -1;
  double saturationValue = 0.0;
  int saturationCount = 0;
  double saturationBinSum = 0.0;

  for (int i = 1; i <= hist->GetNbinsX(); ++i) {
    double current = hist->GetBinContent(i);
    double next = i < hist->GetNbinsX() ? hist->GetBinContent(i + 1) : std::numeric_limits<double>::max();
    double prev = i > 1 ? hist->GetBinContent(i - 1) : std::numeric_limits<double>::max();

    if (!inPositivePeakRegion && current >= positiveThreshold) {
      if (current == prev || current == next) {
        startMaxBin = (startMaxBin == -1) ? i : startMaxBin;
        saturationValue = current; inPositivePeakRegion = true;
        saturationBinSum += i; saturationCount++;
      } else if (current > prev && current > next) {
        positivePeakBins.push_back(i); positivePeakValues.push_back(current);
        foundPositivePeak = true; inPositivePeakRegion = true;
      }
    } else if (inPositivePeakRegion && current < positiveThreshold) {
      if (saturationCount > 0) {
        int centerOfMassBin = static_cast<int>(saturationBinSum / saturationCount + 0.5);
        positivePeakBins.push_back(centerOfMassBin);
        positivePeakValues.push_back(saturationValue);
        startMaxBin = -1; saturationBinSum = 0.0; saturationCount = 0;
      }
      inPositivePeakRegion = false; foundPositivePeak = true;
    }

    if (foundPositivePeak) {
      if (!inNegativePeak && current <= negativeThreshold) {
        inNegativePeak = true; maxNegativePeakValue = current; maxNegativePeakBin = i;
      } else if (inNegativePeak) {
        if (current < maxNegativePeakValue) { maxNegativePeakValue = current; maxNegativePeakBin = i; }
        else if (current == maxNegativePeakValue) { maxNegativePeakBin = i; }
        else if (current > maxNegativePeakValue || i == hist->GetNbinsX()) {
          inNegativePeak = false;
          negativePeakBins.push_back(maxNegativePeakBin);
          negativePeakValues.push_back(maxNegativePeakValue);
          foundPositivePeak = false;
          maxNegativePeakValue = std::numeric_limits<double>::max();
          maxNegativePeakBin = -1;
        }
      }
    }
  }

  if (positivePeakBins.size() == 0) {
    double mx = hist->GetMaximum();
    int mxb = hist->GetMaximumBin();
    positivePeakBins.push_back(mxb);
    positivePeakValues.push_back(mx);
  }
}

// ---- ideal CR-RC^4 cold-electronics response (3 params: t0, A0, tp) ----
static double response_legacy(double* x, double* par) {
  double t = x[0] - par[0];
  double A0 = par[1];
  double tp = par[2];
  double reltime = t / tp;
  double gain = A0 * 1.012;

  double value =
      4.31054 * exp(-2.94809 * reltime) * gain
    - 2.6202  * exp(-2.82833 * reltime) * cos(1.19361 * reltime) * gain
    - 2.6202  * exp(-2.82833 * reltime) * cos(1.19361 * reltime) * cos(2.38722 * reltime) * gain
    + 0.464924 * exp(-2.40318 * reltime) * cos(2.5928 * reltime) * gain
    + 0.464924 * exp(-2.40318 * reltime) * cos(2.5928 * reltime) * cos(5.18561 * reltime) * gain
    + 0.762456 * exp(-2.82833 * reltime) * sin(1.19361 * reltime) * gain
    - 0.762456 * exp(-2.82833 * reltime) * cos(2.38722 * reltime) * sin(1.19361 * reltime) * gain
    + 0.762456 * exp(-2.82833 * reltime) * cos(1.19361 * reltime) * sin(2.38722 * reltime) * gain
    - 2.620200 * exp(-2.82833 * reltime) * sin(1.19361 * reltime) * sin(2.38722 * reltime) * gain
    - 0.327684 * exp(-2.40318 * reltime) * sin(2.5928 * reltime) * gain
    + 0.327684 * exp(-2.40318 * reltime) * cos(5.18561 * reltime) * sin(2.5928 * reltime) * gain
    - 0.327684 * exp(-2.40318 * reltime) * cos(2.5928 * reltime) * sin(5.18561 * reltime) * gain
    + 0.464924 * exp(-2.40318 * reltime) * sin(2.5928 * reltime) * sin(5.18561 * reltime) * gain;

  if (t > 0 && t < 10) return value;
  return 0;
}

// ---- main entry ----
void wf_fitter_ideal(const char* infile, const char* det, int run) {

  TString detS(det);
  detS.ToLower();
  // sampling: HD/VD readout = 512 ns/tick in these magnify files
  g_tick_us = 0.512;

  cout << "[wf_fitter_ideal] det=" << detS << " run=" << run
       << " file=" << infile << " tick=" << g_tick_us << " us" << endl;

  TFile* file = TFile::Open(infile, "READ");
  if (!file || file->IsZombie()) { std::cerr << "Cannot open " << infile << endl; return; }

  // After-correction magnify product is "h_raw".
  TH2I* hist2D = (TH2I*)file->Get("h_raw");
  if (!hist2D) { std::cerr << "Histogram h_raw not found in " << infile << endl; return; }

  gSystem->Exec("mkdir -p txt_files root_files");

  std::ofstream fit_results_file(Form("txt_files/eleccalib_fit_%s_run_%d.txt", detS.Data(), run));
  fit_results_file << "#Ch.\t t\t E(t)\t A_0\t E(A_0)\t t_p\t E(t_p)\t Chi2/DOF" << endl;
  std::ofstream failed_fits_file(Form("txt_files/eleccalib_fishy_%s_run_%d.txt", detS.Data(), run));

  TFile* outputFile = new TFile(Form("root_files/eleccalib_fit_%s_run_%d.root", detS.Data(), run), "RECREATE");
  TTree* fitTree = new TTree("FitResults", "Ideal-response fit results");
  int iFit = 0, fitStatus = 0;
  double prefit_chi2DOF = 0, prefit_chi2 = 0, prefit_dof = 0;
  double prefit_param[3], prefit_paramError[3];
  std::vector<double> fittedValues;
  TH1F* h_peaks_ptr = nullptr;
  fitTree->Branch("iFit", &iFit, "iFit/I");
  fitTree->Branch("fitStatus", &fitStatus, "fitStatus/I");
  fitTree->Branch("prefit_chi2DOF", &prefit_chi2DOF, "prefit_chi2DOF/D");
  fitTree->Branch("prefit_param", prefit_param, "prefit_param[3]/D");
  fitTree->Branch("prefit_paramError", prefit_paramError, "prefit_paramError[3]/D");
  fitTree->Branch("prefit_chi2", &prefit_chi2, "prefit_chi2/D");
  fitTree->Branch("prefit_dof", &prefit_dof, "prefit_dof/D");
  fitTree->Branch("f_ideal", &fittedValues);
  fitTree->Branch("h_peaks", &h_peaks_ptr);

  ROOT::Math::MinimizerOptions::SetDefaultMaxFunctionCalls(500000);

  for (int i = 1; i <= hist2D->GetNbinsX(); ++i) {
    int ch = (int)(hist2D->GetXaxis()->GetBinCenter(i) + 0.5);

    TH1D* projY = hist2D->ProjectionY(Form("projY_ch_%d", ch), i, i);

    bool there_is_nan = false;
    for (int n = 0; n <= projY->GetNbinsX(); ++n)
      if (TMath::IsNaN(projY->GetBinContent(n))) { there_is_nan = true; break; }
    if (there_is_nan) { delete projY; continue; }

    double mx = projY->GetMaximum();
    double mn = projY->GetMinimum();
    projY->SetStats(kFALSE);

    double positiveThreshold = mx / 2.0;
    double negativeThreshold = mn / 2.0;

    std::vector<int> ppB, npB; std::vector<double> ppV, npV;
    FindPeaks(projY, positiveThreshold, negativeThreshold, ppB, ppV, npB, npV);

    // drop a spurious very-early first peak
    if (ppB.size() > 1 && ppB[0] < 100) {
      for (size_t k = 1; k < ppB.size(); ++k) { ppB[k-1] = ppB[k]; ppV[k-1] = ppV[k]; }
      ppB.resize(ppB.size() - 1); ppV.resize(ppV.size() - 1);
    }
    // drop a spurious very-late last peak
    if (ppB.size() > 1 && ppB[ppB.size()-1] > 5750) {
      ppB.resize(ppB.size() - 1); ppV.resize(ppV.size() - 1);
    }
    if (ppB.empty()) { delete projY; continue; }

    // build the first pulse window
    int nbefore = 15, nafter = 100;
    double x_min = ppB[0] - nbefore;
    double x_max = ppB[0] + nafter;
    int binLow = projY->FindBin(x_min);
    int binHigh = projY->FindBin(x_max);
    int nBins = std::abs((int)(x_max - x_min + 1));
    if (nBins == 0) nBins = std::abs(binHigh - binLow + 1);

    TH1F* h_pulse = new TH1F(Form("hist_ch_%d", ch),
                             Form("Pulse Ch %d, AFTER calib", ch), nBins, 0, nBins);
    int newBin = 0;
    for (int k = binLow; k <= binHigh; ++k) {
      newBin++;
      h_pulse->SetBinContent(newBin, projY->GetBinContent(k));
    }
    ScaleXaxis(h_pulse, ScaleX);
    h_pulse->GetXaxis()->SetRangeUser(0, 70);

    // validity checks
    bool valid = true; double sumContents = 0;
    for (int b = 1; b <= h_pulse->GetNbinsX(); ++b) {
      double c = h_pulse->GetBinContent(b);
      if (std::isnan(c)) { valid = false; break; }
      sumContents += c;
    }
    if (valid && std::fabs(sumContents) < 1e-10) valid = false;
    if (!valid || h_pulse->GetEntries() == 0) { delete h_pulse; delete projY; continue; }

    // fit with the ideal response
    TF1* f_ideal = new TF1("f_ideal", response_legacy, 0, 100, 3);
    f_ideal->SetNpx(1000);
    f_ideal->SetParameters(4.5, h_pulse->GetMaximum(), 2.0);
    f_ideal->SetParName(0, "t");
    f_ideal->SetParName(1, "A_{0}");
    f_ideal->SetParName(2, "t_{p}");

    TFitResultPtr r0 = h_pulse->Fit("f_ideal", "RSQ", "", 0, 50.0);

    double chi2 = r0->Chi2();
    double dof = r0->Ndf();
    double chi2dof = dof > 0 ? chi2 / dof : 0;

    fit_results_file << ch << "\t"
      << std::setprecision(10) << r0->Parameter(0) << "\t" << r0->Error(0) << "\t"
      << r0->Parameter(1) << "\t" << r0->Error(1) << "\t"
      << r0->Parameter(2) << "\t" << r0->Error(2) << "\t"
      << chi2dof << endl;

    for (int j = 0; j < 3; ++j) { prefit_param[j] = r0->Parameter(j); prefit_paramError[j] = r0->Error(j); }
    prefit_chi2 = chi2; prefit_dof = dof; prefit_chi2DOF = chi2dof;
    fitStatus = r0->Status(); iFit = ch;
    if (fitStatus != 0) failed_fits_file << ch << "\t status " << fitStatus << endl;
    if (chi2 >= 10000) failed_fits_file << ch << "\t high chi2 " << chi2 << endl;

    fittedValues.clear();
    for (int b = 1; b <= h_pulse->GetNbinsX(); ++b)
      fittedValues.push_back(f_ideal->Eval(h_pulse->GetBinCenter(b)));
    h_peaks_ptr = h_pulse;
    fitTree->Fill();

    delete f_ideal; delete h_pulse; delete projY;
  } // channel loop

  fitTree->Write();
  outputFile->Close();
  fit_results_file.close();
  failed_fits_file.close();
  cout << "[wf_fitter_ideal] done.  results in txt_files/ and root_files/" << endl;
}
