// wf_gain_scan.C -- per-channel gain (dA/dQ) and peaking-time (t_p) extraction
// across a pulser CHARGE SCAN, for the electronics-response calibration plots
// (slope dA/dQ and t_p, "Without Deconvolution" vs "With Deconvolution").
//
// For each magnify file in the scan it fits every channel's pulse with the
// IDEAL CR-RC^4 cold-electronics response (same response_legacy as
// wf_fitter_ideal.C) and records the amplitude A_0 and peaking time t_p.  Then
// per channel it linear-fits A_0 vs injected charge Q -> slope dA/dQ (the gain).
//
// Output (per det+variant): a TTree "gain" with branches
//   ch, slope (dA/dQ), slope_err, tp_mean, tp_at_ref, npoints, chi2dof_line
// written to root_files/gainscan_<det>_<variant>.root, plus a txt summary.
//
// Usage (inside container, dunesw sourced):
//   root -b -q 'wf_gain_scan.C("vd","daq")'
//   root -b -q 'wf_gain_scan.C("vd","raw")'
//   root -b -q 'wf_gain_scan.C("hd","daq")'
//   root -b -q 'wf_gain_scan.C("hd","raw")'
//
// "daq" -> reads hdaq-* magnify files, TH2 "h_daq"  (Without Deconvolution)
// "raw" -> reads hraw-* magnify files, TH2 "h_raw"  (With Deconvolution)

#include <TFile.h>
#include <TTree.h>
#include <TH1D.h>
#include <TH2.h>
#include <TF1.h>
#include <TGraphErrors.h>
#include <TMath.h>
#include <TAxis.h>
#include <TArrayD.h>
#include <TString.h>
#include <TSystem.h>
#include "TFitResult.h"
#include "TFitResultPtr.h"
#include "Math/MinimizerOptions.h"

#include <iostream>
#include <fstream>
#include <iomanip>
#include <map>
#include <vector>
#include <limits>
#include <cmath>

using std::cout;
using std::endl;
using std::vector;

// ---- tick -> microsecond (512 ns/tick in these magnify files) ----
static double g_tick_us = 0.512;

// ---- ADC -> mV conversion, DERIVED from the digitizer (no magic number) ----
// The digitizer maps the voltage window [fullscale_lo, fullscale_hi] linearly onto
// 2^resolution ADC counts, so:  ADC/mV = 2^resolution / (fullscale_hi-fullscale_lo)/1000.
// Values are the PDVD BOTTOM-drifter digitizer from
// pgrapher/experiment/protodunevd/{params,sim}.jsonnet (ident<4):
//   resolution=14, fullscale=[0.2, 1.6] V  ->  16384/1400 = 11.703 ADC/mV
//   (matches the documented "ADC/mV : 11.7" in chndb-resp-bot.jsonnet).
// TOP drifter (ident>=4) uses fullscale=[0.0, 2.0] V -> 8.192 ADC/mV.
static int    g_adc_resolution   = 14;
static double g_fullscale_lo_V    = 0.2;   // V, VD bottom
static double g_fullscale_hi_V    = 1.6;   // V, VD bottom
static double adc_per_mV() {
  double counts = std::pow(2.0, g_adc_resolution);
  return counts / ((g_fullscale_hi_V - g_fullscale_lo_V) * 1000.0);  // ADC per mV
}
static double ScaleX(double x) { return g_tick_us * x; }
static void ScaleAxis(TAxis* a, double (*S)(double)) {
  if (!a) return;
  if (a->GetXbins()->GetSize()) {
    TArrayD X(*(a->GetXbins()));
    for (int i = 0; i < X.GetSize(); i++) X[i] = S(X[i]);
    a->Set((X.GetSize() - 1), X.GetArray());
  } else a->Set(a->GetNbins(), S(a->GetXmin()), S(a->GetXmax()));
}
static void ScaleXaxis(TH1* h, double (*S)(double)) { if (h) ScaleAxis(h->GetXaxis(), S); }

// ---- peak finder (positive pulses + following undershoot) ----
static void FindPeaks(TH1D* hist, double posThr, double negThr,
                      vector<int>& ppB, vector<double>& ppV,
                      vector<int>& npB, vector<double>& npV) {
  ppB.clear(); ppV.clear(); npB.clear(); npV.clear();
  bool inPos = false, foundPos = false, inNeg = false;
  double maxNeg = std::numeric_limits<double>::max(); int maxNegBin = -1;
  int startMaxBin = -1; double satVal = 0.0; int satCnt = 0; double satSum = 0.0;
  for (int i = 1; i <= hist->GetNbinsX(); ++i) {
    double cur = hist->GetBinContent(i);
    double nxt = i < hist->GetNbinsX() ? hist->GetBinContent(i+1) : std::numeric_limits<double>::max();
    double prv = i > 1 ? hist->GetBinContent(i-1) : std::numeric_limits<double>::max();
    if (!inPos && cur >= posThr) {
      if (cur == prv || cur == nxt) { startMaxBin = (startMaxBin==-1)?i:startMaxBin; satVal=cur; inPos=true; satSum+=i; satCnt++; }
      else if (cur > prv && cur > nxt) { ppB.push_back(i); ppV.push_back(cur); foundPos=true; inPos=true; }
    } else if (inPos && cur < posThr) {
      if (satCnt > 0) { int com=(int)(satSum/satCnt+0.5); ppB.push_back(com); ppV.push_back(satVal); startMaxBin=-1; satSum=0; satCnt=0; }
      inPos=false; foundPos=true;
    }
    if (foundPos) {
      if (!inNeg && cur <= negThr) { inNeg=true; maxNeg=cur; maxNegBin=i; }
      else if (inNeg) {
        if (cur < maxNeg) { maxNeg=cur; maxNegBin=i; }
        else if (cur == maxNeg) { maxNegBin=i; }
        else if (cur > maxNeg || i==hist->GetNbinsX()) { inNeg=false; npB.push_back(maxNegBin); npV.push_back(maxNeg); foundPos=false; maxNeg=std::numeric_limits<double>::max(); maxNegBin=-1; }
      }
    }
  }
  if (ppB.empty()) { ppB.push_back(hist->GetMaximumBin()); ppV.push_back(hist->GetMaximum()); }
}

// ---- ideal CR-RC^4 cold-electronics response (par: t0, A0, tp) ----
// This is the IDENTICAL analytic form to the toolkit's Response::coldelec()
// (wire-cell-toolkit/util/src/Response.cxx:311).  coldelec() scales the gain by
//     gain *= 10 * 1.012;
// "a scaling is needed to make the anti-Laplace peak match the expected gain".
// The original macro copy dropped the factor 10 (used *1.012 only), which made the
// fitted A0 come out 10x the true pulse PEAK (the shape peak for A0=1 is then 0.1).
// We use the SAME 10*1.012 as coldelec() so A0 == the true peak amplitude in ADC,
// i.e. the fit reproduces the toolkit response exactly rather than a 10x-off variant.
static double response_legacy(double* x, double* par) {
  double t = x[0]-par[0], A0=par[1], tp=par[2], rt=t/tp, g=A0*10*1.012;
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

// per-channel fit of one TH2 column; returns true on success, fills A0 & tp.
static bool fit_channel(TH2* h2, int binX, double& A0, double& tp, double& chi2dof) {
  TH1D* projY = h2->ProjectionY(Form("py_%d_%lld", binX, (long long)h2->GetUniqueID()), binX, binX);
  for (int n = 0; n <= projY->GetNbinsX(); ++n)
    if (TMath::IsNaN(projY->GetBinContent(n))) { delete projY; return false; }
  double mx = projY->GetMaximum(), mn = projY->GetMinimum();
  vector<int> ppB, npB; vector<double> ppV, npV;
  FindPeaks(projY, mx/2.0, mn/2.0, ppB, ppV, npB, npV);
  if (ppB.size() > 1 && ppB[0] < 100) { for (size_t k=1;k<ppB.size();++k){ppB[k-1]=ppB[k];ppV[k-1]=ppV[k];} ppB.pop_back(); ppV.pop_back(); }
  if (ppB.size() > 1 && ppB.back() > 5750) { ppB.pop_back(); ppV.pop_back(); }
  if (ppB.empty()) { delete projY; return false; }
  int nbefore=15, nafter=100;
  double x_min=ppB[0]-nbefore, x_max=ppB[0]+nafter;
  int binLow=projY->FindBin(x_min), binHigh=projY->FindBin(x_max);
  int nBins=std::abs((int)(x_max-x_min+1)); if (nBins==0) nBins=std::abs(binHigh-binLow+1);
  TH1F* hp = new TH1F(Form("hp_%d_%lld", binX,(long long)h2->GetUniqueID()),"",nBins,0,nBins);
  int nb=0; for (int k=binLow;k<=binHigh;++k){ nb++; hp->SetBinContent(nb, projY->GetBinContent(k)); }
  ScaleXaxis(hp, ScaleX);
  double sum=0; bool valid=true;
  for (int b=1;b<=hp->GetNbinsX();++b){ double c=hp->GetBinContent(b); if(std::isnan(c)){valid=false;break;} sum+=c; }
  if (valid && std::fabs(sum) < 1e-10) valid=false;
  if (!valid || hp->GetEntries()==0){ delete hp; delete projY; return false; }
  TF1* f = new TF1(Form("f_%d_%lld",binX,(long long)h2->GetUniqueID()), response_legacy, 0, 100, 3);
  f->SetNpx(1000);
  f->SetParameters(4.5, hp->GetMaximum(), 2.0);
  TFitResultPtr r = hp->Fit(f, "RSQN", "", 0, 50.0);
  bool ok = (r.Get() != nullptr);
  if (ok) {
    A0 = r->Parameter(1);
    tp = r->Parameter(2);
    double dof = r->Ndf();
    chi2dof = dof > 0 ? r->Chi2()/dof : 0;
  }
  delete f; delete hp; delete projY;
  return ok;
}

// charge (or DAC) per run for each detector scan -------------------------------
struct ScanPoint { int run; double q; const char* tag; };  // tag: magnify run-evt suffix

static vector<ScanPoint> scan_points(const TString& det) {
  // q = injected charge in fC for VD (user-provided); = DAC for HD (convert later)
  if (det == "vd") return {
    {41573,  2.65105, "41573-4"}, {41574, 5.30210, "41574-4"}, {41577, 13.25520, "41577-4"},
    {41582, 26.51050, "41582-4"}, {41587,39.76570, "41587-4"}, {41593, 55.67200, "41593-4"},
    {41597, 66.27620, "41597-4"}, {41602,79.53150, "41602-1"}, {41603, 82.18255, "41603-4"},
  };
  // HD: q = DAC (condb); charge_fC = DAC * (HD fC/DAC, TBD)
  return {
    {30367, 5, "30367-8"}, {30376,10,"30376-8"}, {30385,15,"30385-8"}, {30394,20,"30394-8"},
    {30404,25,"30404-8"}, {30413,30,"30413-8"}, {30421,35,"30421-8"}, {30430,40,"30430-8"},
  };
}

// locate the magnify file for a (det,variant,scanpoint) -----------------------
// OURS variant: both h_daq and h_raw live in the SAME self-reprocessed combined
// magnify ourmag-pdvd-<run>-<evt>.root (made by vd_convert_orig_raw.C).  The
// event-number suffix varies per run (reco_stage1 -> 4, HDF5 41602 -> 1), so we
// glob on the run number instead of using sp.tag.
static TString magnify_path(const TString& det, const TString& /*variant*/, const ScanPoint& sp) {
  TString base = "/exp/dune/data/users/xning/proto-dune-vd/elec_res/output/scan";
  // resolve ourmag-pdvd-<run>-*.root via shell glob (first match)
  TString cmd = Form("ls -1 %s/ourmag-pdvd-%d-*.root 2>/dev/null | head -1", base.Data(), sp.run);
  TString path = gSystem->GetFromPipe(cmd);
  path = path.Strip(TString::kTrailing, '\n');
  return path;
}

void wf_gain_scan_ours(const char* det_c, const char* variant_c) {
  TString det(det_c); det.ToLower();
  TString variant(variant_c); variant.ToLower();
  if (det != "vd" && det != "hd") { std::cerr << "det must be vd|hd\n"; return; }
  if (variant != "daq" && variant != "raw") { std::cerr << "variant must be daq|raw\n"; return; }
  g_tick_us = 0.512;
  ROOT::Math::MinimizerOptions::SetDefaultMaxFunctionCalls(500000);

  const char* h2name = (variant == "daq") ? "h_daq" : "h_raw";
  auto pts = scan_points(det);
  cout << "[gain_scan] det=" << det << " variant=" << variant
       << " (" << (variant=="daq"?"Without":"With") << " Deconvolution)"
       << "  npoints=" << pts.size() << "  TH2=" << h2name << endl;

  // per-channel accumulators: charge[], A0[], and tp at the reference (largest-Q) run
  std::map<int, vector<double>> qByCh, aByCh;
  std::map<int, double> tpRefByCh;   // tp at the highest-charge run with a good fit
  double refQ = -1; for (auto& p : pts) if (p.q > refQ) refQ = p.q;

  for (auto& sp : pts) {
    TString path = magnify_path(det, variant, sp);
    TFile* f = TFile::Open(path, "READ");
    if (!f || f->IsZombie()) { std::cerr << "  MISSING: " << path << endl; continue; }
    TH2* h2 = dynamic_cast<TH2*>(f->Get(h2name));
    if (!h2) { std::cerr << "  no " << h2name << " in " << path << endl; f->Close(); continue; }
    h2->SetDirectory(nullptr);
    static unsigned tag = 0; h2->SetUniqueID(++tag);
    int nfit = 0;
    for (int bx = 1; bx <= h2->GetNbinsX(); ++bx) {
      int ch = (int)(h2->GetXaxis()->GetBinCenter(bx) + 0.5);
      double A0=0, tp=0, c2=0;
      if (!fit_channel(h2, bx, A0, tp, c2)) continue;
      if (A0 <= 0 || tp <= 0 || tp > 5) continue;      // basic sanity
      qByCh[ch].push_back(sp.q);
      aByCh[ch].push_back(A0);
      if (std::fabs(sp.q - refQ) < 1e-6) tpRefByCh[ch] = tp;
      nfit++;
    }
    cout << "  run " << sp.run << " Q=" << sp.q << " : " << nfit << " channels fit" << endl;
    delete h2; f->Close();
  }

  // per-channel line fit A0 vs Q -> slope (gain)
  gSystem->Exec("mkdir -p root_files txt_files");
  TFile* out = new TFile(Form("root_files/gainscan_%s_ours_%s.root", det.Data(), variant.Data()), "RECREATE");
  TTree* t = new TTree("gain", "per-channel gain scan");
  // slope      = gain in mV/fC  (the physical answer)
  // slope_adc  = raw fit slope dA0/dQ in ADC/fC  (A0 = true peak ADC, *10 fix applied)
  // slope/slope_adc == 1/(ADC/mV)
  int ch=0, npts=0;
  double slope=0, slope_adc=0, slope_err=0, slope_err_adc=0, tp_ref=0, line_chi2dof=0, inter=0;
  const double adcmv = adc_per_mV();   // ADC/mV from digitizer fullscale+resolution
  t->Branch("ch",&ch,"ch/I");
  t->Branch("slope",&slope,"slope/D");                 // mV/fC
  t->Branch("slope_adc",&slope_adc,"slope_adc/D");     // ADC/fC
  t->Branch("slope_err",&slope_err,"slope_err/D");     // mV/fC
  t->Branch("slope_err_adc",&slope_err_adc,"slope_err_adc/D");
  t->Branch("intercept",&inter,"intercept/D");
  t->Branch("tp_ref",&tp_ref,"tp_ref/D"); t->Branch("npoints",&npts,"npoints/I");
  t->Branch("line_chi2dof",&line_chi2dof,"line_chi2dof/D");

  cout << "[gain_scan] ADC/mV = " << adcmv << "  (2^" << g_adc_resolution
       << " / (" << g_fullscale_hi_V << "-" << g_fullscale_lo_V << ")V) -> slope reported in mV/fC\n";

  std::ofstream ofs(Form("txt_files/gainscan_%s_ours_%s.txt", det.Data(), variant.Data()));
  ofs << "#ch slope(mV/fC) slope_err(mV/fC) slope_adc(ADC/fC) intercept tp_ref npoints line_chi2dof\n";

  int nGood = 0;
  for (auto& kv : qByCh) {
    ch = kv.first;
    auto& Q = kv.second; auto& A = aByCh[ch];
    npts = (int)Q.size();
    if (npts < 3) continue;                  // need >=3 points for a meaningful slope
    TGraph g(npts, &Q[0], &A[0]);
    TF1 line("line","[0]+[1]*x", 0, 1e9);
    line.SetParameters(0, A.back()/std::max(Q.back(),1e-9));
    TFitResultPtr r = g.Fit(&line, "SQN");
    if (r.Get()==nullptr) continue;
    inter = r->Parameter(0);
    slope_adc     = r->Parameter(1);          // ADC/fC (A0 in true peak ADC)
    slope_err_adc = r->Error(1);
    slope     = slope_adc     / adcmv;         // -> mV/fC
    slope_err = slope_err_adc / adcmv;         // -> mV/fC
    double dof = npts - 2;
    line_chi2dof = dof > 0 ? r->Chi2()/dof : 0;
    tp_ref = tpRefByCh.count(ch) ? tpRefByCh[ch] : 0;
    if (slope_adc <= 0) continue;
    t->Fill();
    ofs << ch << " " << std::setprecision(8) << slope << " " << slope_err << " "
        << slope_adc << " " << inter << " " << tp_ref << " " << npts << " " << line_chi2dof << "\n";
    nGood++;
  }
  t->Write(); out->Close(); ofs.close();
  cout << "[gain_scan] wrote " << nGood << " channels -> root_files/gainscan_"
       << det << "_" << variant << ".root (+ txt)" << endl;
}
