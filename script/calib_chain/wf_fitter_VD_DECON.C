#include <TFile.h>
#include <TTree.h>
#include <TBranch.h>
#include <TH1D.h>
#include <TH2D.h>
#include <TF1.h>
#include <TH2.h>
#include <TH1.h>
#include <TLatex.h>
#include <TStyle.h>
#include <TPad.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <array>
#include <TCanvas.h>
#include <TPad.h>
#include "TGraph.h"
#include <TGraphErrors.h>
#include <TSystem.h>
#include <math.h>
#include <TROOT.h>
#include <TAttLine.h>
#include <TLegend.h>
#include <TString.h>
#include <cmath> // For std::abs
#include "TFitResult.h"
#include <fstream>
#include <TFitResult.h>
#include <TFitResultPtr.h>
#include <limits>
#include "Math/MinimizerOptions.h"
#include <unordered_set>


#include "TPaveStats.h"
#include "TPaveLabel.h"

using std::cout;
using std::cin;
using std::endl;

std::vector<int> positivePeakBins;
std::vector<double> positivePeakValues;
std::vector<int> negativePeakStartBins;
std::vector<int> negativePeakEndBins;
std::vector<double> negativePeakStartValues;
std::vector<double> negativePeakEndValues;





Double_t ScaleX(Double_t x){
  Double_t v;
  v = (0.512 * x); // to scale ticks to microseconds
  return v;
}

void ScaleAxis(TAxis *a, Double_t (*Scale)(Double_t)){

    if (!a) return;
    if (a->GetXbins()->GetSize())
        {
            TArrayD X(*(a->GetXbins()));
            for(Int_t i = 0; i < X.GetSize(); i++) X[i] = Scale(X[i]);
            a->Set((X.GetSize() - 1), X.GetArray());
        }
    else
        {
            a->Set( a->GetNbins(),
                    Scale(a->GetXmin()),
                    Scale(a->GetXmax()) );
        }
    return;

    }

void ScaleXaxis(TH1 *h, Double_t (*Scale)(Double_t)){

    if (!h) return;
    ScaleAxis(h->GetXaxis(), Scale);
    return;

    }



void FindPeaks(TH1D* hist, double positiveThreshold, double negativeThreshold,
               std::vector<int>& positivePeakBins, std::vector<double>& positivePeakValues,
               std::vector<int>& negativePeakBins, std::vector<double>& negativePeakValues) {

    positivePeakBins.clear();
    positivePeakValues.clear();
    negativePeakBins.clear();
    negativePeakValues.clear();

    bool inPositivePeakRegion = false;
    bool foundPositivePeak = false;
    double maxNegativePeakValue = std::numeric_limits<double>::max();
    int maxNegativePeakBin = -1;
    bool inNegativePeak = false;

    int startMaxBin = -1; // NEW: Start of the saturation peak region
    double saturationValue = 0.0; // NEW: Value of the saturation peak
    int saturationCount = 0; // Number of bins in the saturation region
    double saturationBinSum = 0.0; // Sum of bin indices in the saturation region


    for (int i = 1; i <= hist->GetNbinsX(); ++i) {

        double current = hist->GetBinContent(i);
        double next = i < hist->GetNbinsX() ? hist->GetBinContent(i + 1) : std::numeric_limits<double>::max();
        double prev = i > 1 ? hist->GetBinContent(i - 1) : std::numeric_limits<double>::max();

        //*** NEW: If there is saturation, keep the middle bin as the peak:

        if (!inPositivePeakRegion && current >= positiveThreshold) {
            if (current == prev || current == next) {
                // Start or continuation of a saturation peak region
                startMaxBin = (startMaxBin == -1) ? i : startMaxBin;
                saturationValue = current;
                inPositivePeakRegion = true;
                saturationBinSum += i;
                saturationCount++;
            } else if (current > prev && current > next) {
                // Regular peak (not saturated)
                positivePeakBins.push_back(i);
                positivePeakValues.push_back(current);
                foundPositivePeak = true;
                inPositivePeakRegion = true;
            }
        } else if (inPositivePeakRegion && current < positiveThreshold) {
            // End of saturation peak region
            if (saturationCount > 0) {
                int centerOfMassBin = static_cast<int>(saturationBinSum / saturationCount + 0.5);
                positivePeakBins.push_back(centerOfMassBin);
                positivePeakValues.push_back(saturationValue);
                startMaxBin = -1;
                saturationBinSum = 0.0;
                saturationCount = 0;
            }
            inPositivePeakRegion = false;
            foundPositivePeak = true;
        }



        ///*****


        // Negative peak tracking after a positive peak
        if (foundPositivePeak) {
            if (!inNegativePeak && current <= negativeThreshold) {
                // Entering a negative peak region
                inNegativePeak = true;
                maxNegativePeakValue = current;
                maxNegativePeakBin = i; // Provisionally, this might be updated
            } else if (inNegativePeak) {
                if (current < maxNegativePeakValue) {
                    // Found a new max negative value within the peak
                    maxNegativePeakValue = current;
                    maxNegativePeakBin = i;
                } else if (current == maxNegativePeakValue) {
                    // Extend the peak to the last bin with the same value
                    maxNegativePeakBin = i;
                } else if (current > maxNegativePeakValue || i == hist->GetNbinsX()) {
                    // Exiting the negative peak region or end of histogram
                    inNegativePeak = false;
                    negativePeakBins.push_back(maxNegativePeakBin);
                    negativePeakValues.push_back(maxNegativePeakValue);
                    foundPositivePeak = false; // Reset for the next sequence
                    // Prepare for the next negative peak
                    maxNegativePeakValue = std::numeric_limits<double>::max();
                    maxNegativePeakBin = -1;
                }
            }
        }
    }

    if (positivePeakBins.size()==0) { // If somehow positive peaks weren't found, just get te maximum one.

        cout << "WARNING: No positive peaks were found!" << endl;
        double max = hist->GetMaximum();
        double max_bin = hist->GetMaximumBin();

        positivePeakBins.push_back(max_bin);
        positivePeakValues.push_back(max);
    }

    std::cout << "****************************************\n";


} //END FUNCTION

/////////////////////////////////////////////////////////////////////////////////////////

bool isHistogramTooNoisy(TH1D* hist, double maxAllowedEntries){

    if(hist->GetEntries() >= maxAllowedEntries){
        return true;
    }else return false;

}


/////////////////////////////////////////////////////////////////////////////////////////

//  Dealing with noisy histograms:

bool FindPeaksWithNoiseCheck(TH1D* hist, double positiveThreshold, double negativeThreshold,
                             std::vector<int>& positivePeakBins, std::vector<double>& positivePeakValues,
                             std::vector<int>& negativePeakBins, std::vector<double>& negativePeakValues,
                             double maxAllowedEntries){

    if (isHistogramTooNoisy(hist, maxAllowedEntries)) {
        std::cout << "Histogram is too noisy and will be skipped." << std::endl;
        return false;
    }


    FindPeaks(hist, positiveThreshold, negativeThreshold, positivePeakBins, positivePeakValues, negativePeakBins, negativePeakValues);

    return true;
}



///////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////DEFINING FIT FUNCTIONS/////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////


double response_legacy(double *x, double *par){
  Double_t t = x[0]-par[0];
  Double_t A0 = par[1];
  Double_t tp = par[2];

  Double_t reltime = t / tp;
  Double_t gain = A0* 1.012;

  Double_t value = 4.31054 * exp(-2.94809 * reltime) * gain - 2.6202 * exp(-2.82833 * reltime) * cos(1.19361 * reltime) * gain -
           2.6202 * exp(-2.82833 * reltime) * cos(1.19361 * reltime) * cos(2.38722 * reltime) * gain +
           0.464924 * exp(-2.40318 * reltime) * cos(2.5928 * reltime) * gain +
           0.464924 * exp(-2.40318 * reltime) * cos(2.5928 * reltime) * cos(5.18561 * reltime) * gain +
           0.762456 * exp(-2.82833 * reltime) * sin(1.19361 * reltime) * gain -
           0.762456 * exp(-2.82833 * reltime) * cos(2.38722 * reltime) * sin(1.19361 * reltime) * gain +
           0.762456 * exp(-2.82833 * reltime) * cos(1.19361 * reltime) * sin(2.38722 * reltime) * gain -
           2.620200 * exp(-2.82833 * reltime) * sin(1.19361 * reltime) * sin(2.38722 * reltime) * gain -
           0.327684 * exp(-2.40318 * reltime) * sin(2.5928 * reltime) * gain +
           +0.327684 * exp(-2.40318 * reltime) * cos(5.18561 * reltime) * sin(2.5928 * reltime) * gain -
           0.327684 * exp(-2.40318 * reltime) * cos(2.5928 * reltime) * sin(5.18561 * reltime) * gain +
           0.464924 * exp(-2.40318 * reltime) * sin(2.5928 * reltime) * sin(5.18561 * reltime) * gain;


  if (t > 0 and t<10)
    return value;
  else
    return 0;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////



void PlotandFitWaveforms(int run) {

   cout << "----------> Running fits for run number: " << run << endl;


    std::string label = "AFTER"; // %s label.c_str()

    // Open the ROOT file to read the data
//     TFile *file = new TFile(Form("magnify_files/hdaq-pdvd-magnify-%d-4.root", run), "READ");   // BEFORE CALIBRATION

     TFile *file = new TFile(Form("magnify_files/hraw-pdvd-magnify-%d-4.root", run), "READ"); //AFTER CALIBRATION


    // Retrieve the TH2I histogram
//     TH2I *hist2D = (TH2I*)file->Get("h_daq"); // BEFORE
     TH2I *hist2D = (TH2I*)file->Get("h_raw"); //AFTER

    if (!hist2D) {
        std::cerr << "Histogram not found!" << std::endl;
        return;
    }

    std::ofstream fit_results_file(Form("txt_files/fit_results_run_%s_%d_no_avg_3.txt",label.c_str(),run));
    fit_results_file << "#Ch. # \t t \t E(t) \t A_0 \t E(A_0) \t t_p \t E(t_p) \t Chi2/DOF" << endl;

    std::ofstream failed_fits_file(Form("txt_files/fishy_fits_%s_run_", label.c_str()) + std::to_string(run) + "_no_avg_3.txt");



    cout << "----------> Running fits for run number: " << run << endl;


   int fitStatus;
   int iFit = 0;

   double prefit_chi2DOF;
   double prefit_param[3];
   double prefit_paramError[3];
   double prefit_chi2;
   double prefit_dof;

   std::vector<double> fittedValues;
   TH1F* h_averaged_ptr = nullptr;

   TFile *outputFile = new TFile(Form("root_files/fit_results_%s_run_%d_no_avg_3.root",label.c_str(),run), "RECREATE");


   TTree *fitTree = new TTree(Form("FitResults_%s",label.c_str()), "Fit Results Tree");


   fitTree->Branch(Form("iFit_%s",label.c_str()), &iFit, "iFit/I");
   fitTree->Branch(Form("fitStatus_%s",label.c_str()), &fitStatus, "fitStatus/I");

   fitTree->Branch(Form("prefit_chi2DOF_%s",label.c_str()), &prefit_chi2DOF, "prefit_chi2DOF/D");
   fitTree->Branch(Form("prefit_param_%s",label.c_str()), prefit_param, "prefit_param[3]/D");
   fitTree->Branch(Form("prefit_paramError_%s",label.c_str()), prefit_paramError, "prefit_paramError[3]/D");
   fitTree->Branch(Form("prefit_chi2_%s",label.c_str()), &prefit_chi2, "prefit_chi2/D");
   fitTree->Branch(Form("prefit_dof_%s",label.c_str()), &prefit_dof, "prefit_dof/D");



   fitTree->Branch(Form("f_ideal_%s",label.c_str()), &fittedValues);
   fitTree->Branch(Form("h_peaks_%s",label.c_str()), &h_averaged_ptr);


   ///////////////////////////////////////////////////////////////////////////////////////////

    std::vector<int> selectedChannels = {3,4243,4244,294,956,1483,2658,2706,2728,5472,
    103,3262,3263,5798,6046,6090};

    //for(int i : selectedChannels){
    for(int i = 1; i <= hist2D->GetNbinsX() ; ++i) {


      cout << "\n\n********* Ch. " << (i-1) << " *********\n" << endl;

      TString projYName = TString::Format("projY_ch_%d", (i-1));
      TH1D *projY = hist2D->ProjectionY(projYName, i, i);
      //std::cout << "Number of Bins in ProjY(h_daq):  " << projY->GetNbinsX() << endl;


      bool there_is_nan = false;

      for(int n = 0 ; n<= projY->GetNbinsX(); ++n) {
          if(TMath::IsNaN(projY->GetBinContent(n))){
              there_is_nan = true;
              break;
          }
      }

      if(there_is_nan) continue;


      double max = projY->GetMaximum();
      double min = projY->GetMinimum();


      // Giving a nice format to plots in case we want to plot raws:
      projY->SetStats(kFALSE);
      projY->GetXaxis()->SetTitle("Ticks");
      projY->GetYaxis()->SetTitle("ADC counts");
      projY->GetYaxis()->SetRangeUser(-9000, 9000);
      //projY->GetXaxis()->SetRangeUser(5500, 6000);
      projY->SetLineWidth(1);
      projY->SetLineColor(kBlue);
      projY->GetYaxis()->SetLabelSize(0.025);
      projY->GetYaxis()->SetTitleSize(0.03);
      projY->Draw();
      //gPad -> Print(Form("plots/raws/WF_Raw_Ch_%i_Run_%i_HD.png",(i-1),run));


      double positiveThreshold = max/2.0;
      double negativeThreshold = min/2.0;

      std::vector<int> positivePeakBins;
      std::vector<double> positivePeakValues;
      std::vector<int> negativePeakBins;
      std::vector<double> negativePeakValues;

      bool wasProcessed = FindPeaksWithNoiseCheck(projY, positiveThreshold, negativeThreshold, positivePeakBins, positivePeakValues, negativePeakBins, negativePeakValues, 1000000);

      if(wasProcessed==0) continue;

      //---------------------------------------------------------


      if(positivePeakBins.size()>1 && positivePeakBins[0]< 100){


         for (size_t i = 1; i < positivePeakBins.size(); ++i) {
               positivePeakBins[i-1] = positivePeakBins[i];
         }

         for (size_t i = 1; i < positivePeakValues.size(); ++i) positivePeakValues[i-1] = positivePeakValues[i];

         positivePeakBins.resize(positivePeakBins.size() - 1);
         positivePeakValues.resize(positivePeakValues.size() - 1);

         cout << "First POSITIVE peak was discarded. New peaks are at:\n " << endl;
         for (size_t i = 0; i < positivePeakBins.size(); ++i) {
               std::cout << "Bin: " << positivePeakBins[i] << ", Value: " << positivePeakValues[i] << "\n";
         }

      }


      //---------------------------------------------------------


      if (positivePeakBins.size()<= 0) cout << "**************** WARNING: No peaks were found! *******" << endl;

      if (positivePeakBins.size()<= 1) cout << "**************** WARNING: Only one peak found! *******" << endl;

      if( positivePeakBins.size()>1 && positivePeakBins[positivePeakBins.size() -1] > 5750 ){

         positivePeakBins.resize(positivePeakBins.size() - 1);
         positivePeakValues.resize(positivePeakValues.size() - 1);

         cout << "Last POSITIVE peak was discarded. New peaks are at:\n " << endl;
         for (size_t i = 0; i < positivePeakBins.size(); ++i) {
               std::cout << "Bin: " << positivePeakBins[i] << ", Value: " << positivePeakValues[i] << "\n";
         }

      }
      //---------------------------------------------------------

      std::vector<TH1F*> h_peaks;

      int nbefore = 15; // Number of samples before positive peak
      int nafter = 100; // Number of samples after positive peak

      double x_min; // This would be tick0
      double x_max;
      double binLow;
      double binHigh;
      int nBins;


      for(int j=0; j<positivePeakBins.size(); ++j){

         if(positivePeakBins.size() == 1){

               x_min = positivePeakBins[0]- nbefore;
               x_max = positivePeakBins[0] + nafter;

         }else{

               x_min = positivePeakBins[j]- nbefore;
               x_max = positivePeakBins[j] + nafter;

         }

         binLow = projY->FindBin(x_min);
         binHigh = projY->FindBin(x_max);

         nBins = abs(x_max - x_min + 1);
         if (nBins==0) nBins = abs(binHigh - binLow + 1);


         h_peaks.push_back(new TH1F(Form("hist_%d_channel_%i", j, i-1), Form("Pulse %i, Channel %i, %s Correction", j, i-1,label.c_str()), nBins, 0, nBins));

         int newBinZero = 0;

         for(int k = binLow; k <= binHigh; ++k){

            newBinZero = newBinZero + 1;

            double binContent = projY->GetBinContent(k);
            h_peaks[j]->SetBinContent(newBinZero, binContent);

         }


         ScaleXaxis(h_peaks[j], ScaleX);
         h_peaks[j]->GetXaxis()->SetRangeUser(0,70);

         h_peaks[j]->SetLineColor(kBlue);
         h_peaks[j]->GetXaxis()->SetTitle("Ticks");
         h_peaks[j]->GetYaxis()->SetTitle("ADC counts");
         h_peaks[j]->SetLineWidth(1);
         h_peaks[j]->SetLineColor(4);
         h_peaks[j]->GetYaxis()->SetLabelSize(0.025);
         h_peaks[j]->GetYaxis()->SetTitleSize(0.03);
         h_peaks[j]->Draw("HIST");
         //gPad -> Print(Form("plots/non-fitted/Ch_%i_WF_%i_Run_%i_HD.png",(i-1),j,run));


      } // After this FOR, individual pulse histograms have been created and stored in h_peaks[j].


      h_averaged_ptr = h_peaks[0];

      //---------Skipping problematic histograms:------------------------------

      if (!h_peaks[0]) {
            std::cerr << "Histogram not found: histogram_name_" << i-1 << std::endl;
            continue;
        }

      if (h_peaks[0]->GetEntries() == 0) {
        std::cerr << "Histogram has no entries: h_peaks[0]_ch_" << i-1 << "_" << label << std::endl;
        continue;
      }


      // Check for NaN or all zero/tiny entries
      bool validHistogram = true;
      double sumContents = 0;
      int numBins = h_peaks[0]->GetNbinsX();

      // Iterate over the bins to check for NaN or zero entries
      for (int bin = 1; bin <= numBins; ++bin) {
          double binContent = h_peaks[0]->GetBinContent(bin);

          // Check for NaN
          if (std::isnan(binContent)) {
              std::cerr << "Histogram contains NaN values:h_peaks[0]_ch_" << i-1 << "_" << label << std::endl;
              validHistogram = false;
              break;
          }

          sumContents += binContent;
      }

      const double tinyThreshold = 1e-10;

      if (validHistogram && fabs(sumContents) < tinyThreshold) {
          std::cerr << "Histogram is effectively zero: h_peaks[0]_ch_" << i-1 << "_" << label << std::endl;
          validHistogram = false;
      }

      if (!validHistogram) {
          continue;  // Skip this histogram if it's invalid
      }

      //-------------------------------------------------------------------------------------



      /////////////////////////////////////////////////////////////////////////////////
      // FITTING THE WAVEFORMS:
      /////////////////////////////////////////////////////////////////////////////////


      ROOT::Math::MinimizerOptions::SetDefaultMaxFunctionCalls(500000);

      // Simple fit here: using Ideal electronics response
      TF1 *f_ideal = new TF1("f_ideal", response_legacy, 0,100,3);
        f_ideal->SetNpx(1000);

      TF1* bestFitFunc;
      TFitResultPtr bestFit;

      double max_wf = h_peaks[0]->GetMaximum();

      f_ideal -> SetParameters(4.5, max_wf, 2.0);
      TFitResultPtr r0 =  h_peaks[0] -> Fit("f_ideal","RS","",0,50.0);

      f_ideal -> SetParName(0,"t");
      f_ideal -> SetParName(1,"A_{0}");
      f_ideal -> SetParName(2,"t_{p}");


      double chi2_0 = r0->Chi2();
      double dof_0 = r0->Ndf();
      double chi2PerDof_0 = dof_0 > 0 ? chi2_0 / dof_0 : 0;
      prefit_chi2 = chi2_0;
      prefit_dof = dof_0;


       fit_results_file << i-1 << "\t"
       << setprecision(10) << r0->Parameter(0) << "\t" << setprecision(10) << r0->Error(0) << "\t"
       << setprecision(10) << r0->Parameter(1) << "\t" << setprecision(10) << r0->Error(1)  << "\t"
       << setprecision(10) << r0->Parameter(2) << "\t" << setprecision(10) << r0->Error(2) << "\t"
       << setprecision(10) << chi2PerDof_0 << endl;


      // Loop over parameters to store them in arrays
      for (int j = 0; j < 3; j++) {
         prefit_param[j] = r0->Parameter(j);
         prefit_paramError[j] = r0->Error(j);
      }

      // Storing chi2/DOF
      prefit_chi2DOF = chi2PerDof_0;

      if(chi2_0 >= 10000){// Completely arbitrary
         failed_fits_file << i-1 << "\t High Chi2: \t" << chi2_0 << endl;

      }


      bestFit = r0;
      bestFitFunc = f_ideal;

      int fit_Status = r0->Status();
      fitStatus = fit_Status;

      // Save channel in Tree:
      iFit = i-1;

      fittedValues.clear();

      for (int bin = 1; bin <= h_peaks[0]->GetNbinsX(); ++bin) {

         double x = h_peaks[0]->GetBinCenter(bin);
         double y_fitted = bestFitFunc->Eval(x);
         fittedValues.push_back(y_fitted);
      }

      fitTree->Fill();

      if(fit_Status != 0) {
         failed_fits_file << i-1 << "\t" << fit_Status << endl;

      }

        int nEntries = h_peaks[0]->GetEntries();

        TPaveText* pave = new TPaveText(0.6, 0.55, 0.85, 0.85, "NDC");
        pave->SetFillColor(19);
        pave->SetBorderSize(1);

        pave->AddText(Form("Entries = %d", nEntries));
        pave->AddText(Form("#chi^{2} / ndf = %.3f", chi2_0/dof_0));
        pave->AddText(Form("t  = %.3f #pm %.3f", r0->Parameter(0), r0->Error(0)));
        pave->AddText(Form("A_{0}  = %.2f #pm %.2f", r0->Parameter(1), r0->Error(1)));
        pave->AddText(Form("t_{p} = %.3f #pm %.3f", r0->Parameter(2), r0->Error(2)));

        pave->Draw();

        delete f_ideal;
        for (size_t i = 0; i < h_peaks.size(); ++i) delete h_peaks[i];



    } //END FOR Channels



    fitTree->Write();
    outputFile->Close();

    fit_results_file.close();
    failed_fits_file.close();


} // END OF FITTER Function


////////////////////////////////////////////////////////////////////////////////


void wf_fitter_CORRECTION(int run) {

    PlotandFitWaveforms(run);
}





