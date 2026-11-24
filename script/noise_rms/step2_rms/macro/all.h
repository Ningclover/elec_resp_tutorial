#include <chrono>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

#include "canvas/Utilities/InputTag.h"
// Full lar::sparse_vector<float> definition before gallery pulls in recob::Wire
// / OpWaveform, so cling can parse the lardataobj dictionary payload (else
// AutoParse fails under lardataobj v10_05_01).
#include "lardataobj/Utilities/sparse_vector.h"
#include "gallery/Event.h"

#include "TFile.h"
#include "TH1F.h"
#include "TInterpreter.h"
#include "../plotstyle.h"
#include "TROOT.h"

int nticks = 6400;
const int nchans = 12288;
    const int nb = 49; // color intervals at colorbar, should be odd to keep white @0
    static Int_t colors[nb];
    static Int_t colors2[nb];

TString outname;
int runNo;
int evtNo;
double gain;
vector<int> evtNo_list;
vector<int> total_power;
vector<double> mean_RMS;
vector<double> mean_RMS_AfN;
vector<int> n_dead_chnl;
// vector<vector<int>> dead_chnl;
vector<int> dead_chnl_h;
vector<int> dead_chnl_l;
vector<double> mean_baseline;
vector<vector<vector<double>>> noise_spectra_peak_all;
vector<vector<double>> noise_spectra_peak(4,vector<double>(3));
TString time_string;
double ADC2ENC[]={68.4, 38.1}; // e/ADC for 7.8mV/fC and 14mV/fC
double ratio_ENC;
// Top drift (CRP 2,3) is read out by the top-drift electronics (TDE), whose
// ENC conversion follows the LARZIC ASIC spec: 2 V window over a 14-bit ADC
// with an 11 mV/fC conversion factor.  File scope so both the RMS and the
// FFT code can use it; the bottom drift uses the cold-electronics ratio_ENC
// selected in Draw_RMS().
const double ADC2ENC_TOP = (2000.0/16384.0)/11.0*6241.509126; // = 69.3 e-/ADC (TDE)
// TFile *f1;
    TH1F *h_sob_u_ALL;
    TH1F *h_sob_v_ALL;
    TH1F *h_sob_w_ALL;
    TH1F *h_sob_u_AN_ALL;
    TH1F *h_sob_v_AN_ALL;
    TH1F *h_sob_w_AN_ALL;

TH1I *h_baseline = new TH1I("h_baseline", "Pedestal", nchans, -0.5, nchans - 0.5);
TH1I *h_nHit = new TH1I("h_nHit", "h_nHit", nchans, -0.5, nchans - 0.5);
TH1F *h_nHit_ADC = new TH1F("h_nHit_ADC", "Total charge of Hit", nchans, -0.5, nchans - 0.5);

TH2F *h_orig ;
TH2F *h_orig_ANf ;
TH2F *h_orig_sep[4][3];
TH2F *h_orig_ANf_sep[4][3];
TH2F *h_decon_sep[4][3];
// TH1F *h_rms = new TH1F("h_rms", "raw_rms", nchans, -0.5, nchans - 0.5);
// TH1F *h_rms_ANf = new TH1F("h_rms_ANf", "raw_rms After Nf", nchans, -0.5, nchans - 0.5);
TH1F *h_rms;
TH1F *h_rms_ANf;
TH1F *h_dd_chnl;
TH1F *hfft[4][3];
TH1F *hfft_ANf[4][3];
TH2F *h_cov[4][3];
TH2F *h_cov_ANf[4][3];
TH1F *hfft_ic[2][2];
TH1F *hfft_ic_ANf[2][2];
TVector3 wire_plane_dir[4][3];
// void reset_all_hist(){
//     h_baseline->Reset();

// }
const char *plane_name[3] = {"u", "v", "w"};
int channels[] = {0, 952, 1904, 3072};
int APA_number[] = {1, 2, 3, 4};

double channel_start[4][3];
double channel_end[4][3];

TCanvas *c0 = new TCanvas("c0", "c0", 800, 600);

struct track_info {
    std::string file_name;
    int skip;
    int channel_start[3]; // 0: u, 1: v, 2: w
    int channel_end[3];   // 0: u, 1: v, 2: w
    double time_start;
    double time_end;
    double st_position[3] = {0, 0, 0}; // start position in x, y, z
    double ed_position[3] = {0, 0, 0}; // end position in x, y, z
    TString ID;
    int APA;
    // TH1F *h_sob_u;
    // TH1F *h_sob_v;
    // TH1F *h_sob_w;
    // TH1F *h_sob_u_AN;
    // TH1F *h_sob_v_AN;
    // TH1F *h_sob_w_AN;
};


void processChannelGroups(TH2F *h2, TString tag = "ori", const char* filename = "channel_group.txt") {
    
    if (!h2) {
        std::cerr << "Error: Input histogram is null!" << std::endl;
        return;
    }
    
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open file " << filename << std::endl;
        return;
    }
    
    std::vector<std::vector<int>> groups;
    std::string line;
    
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        
        std::vector<int> group;
        std::istringstream iss(line);
        int channel;
        
        while (iss >> channel) {
            if (channel >= 0 && channel < 12288) {
                group.push_back(channel);
            }
        }
        
        if (!group.empty()) {
            groups.push_back(group);
        }
    }
    file.close();
    
    std::cout << "Read " << groups.size() << " groups from file" << std::endl;
    
    int nbinsY = h2->GetNbinsY();
    double ymin = h2->GetYaxis()->GetXmin();
    double ymax = h2->GetYaxis()->GetXmax();
    
    std::vector<TH2D*> groupHists;
    
    for (size_t i = 0; i < groups.size()/4; i++) {
        TString histName = Form("group_%zu", i*4);
        TString histTitle = Form("Group %zu (%zu channels)", i, groups[i].size()*4);
        
        
        TH2D* groupHist = new TH2D(histName, histTitle, 
                                   64*4, 0, 64*4,
                                   nbinsY, ymin, ymax);
        
        groupHist->SetXTitle("Channel Number");
        groupHist->SetYTitle(h2->GetYaxis()->GetTitle());
        int k=0;
        for (int chan : groups[i*4]) {
            k++;
            for (int ybin = 1; ybin <= nbinsY; ybin++) {
                double content = h2->GetBinContent(chan + 1, ybin);
                if (content != 0) {
                    double y = h2->GetYaxis()->GetBinCenter(ybin);
                    groupHist->Fill(k, y, content);
                }
            }
        }
        for (int chan : groups[i*4+1]) {
            k++;
            for (int ybin = 1; ybin <= nbinsY; ybin++) {
                double content = h2->GetBinContent(chan + 1, ybin);
                if (content != 0) {
                    double y = h2->GetYaxis()->GetBinCenter(ybin);
                    groupHist->Fill(k, y, content);
                }
            }
        }
        for (int chan : groups[i*4+2]) {
            k++;
            for (int ybin = 1; ybin <= nbinsY; ybin++) {
                double content = h2->GetBinContent(chan + 1, ybin);
                if (content != 0) {
                    double y = h2->GetYaxis()->GetBinCenter(ybin);
                    groupHist->Fill(k, y, content);
                }
            }
        }
        for (int chan : groups[i*4+3]) {
            k++;
            for (int ybin = 1; ybin <= nbinsY; ybin++) {
                double content = h2->GetBinContent(chan + 1, ybin);
                if (content != 0) {
                    double y = h2->GetYaxis()->GetBinCenter(ybin);
                    groupHist->Fill(k, y, content);
                }
            }
        }
        
        groupHists.push_back(groupHist);
    }
    
    gStyle->SetOptStat(0);
    gStyle->SetPalette(1);
    
    int nGroups = groupHists.size();

   
    // gStyle->SetPalette(kLightTemperature);

    for (int i = 0; i < nGroups; i++) {

        c0->cd();
        // canvas->cd(i + 1);
        // gPad->SetLeftMargin(0.12);
        // gPad->SetRightMargin(0.15);
        groupHists[i]->Draw("COLZ");
        groupHists[i]->GetZaxis()->SetRangeUser(-50, 50);
        c0->SaveAs(outname + tag+Form("femb_%d_%d_%d.png", runNo, evtNo,i));
        
        // TText* text = new TText();
        // text->SetNDC();
        // text->SetTextSize(0.04);
        // text->DrawText(0.15, 0.9, Form("Group %d", i));
    }
    
    // std::cout << "Saved channel_groups.png" << std::endl;
    
    TFile* outFile = new TFile(outname + tag+"_channel_groups.root", "RECREATE");
    for (auto hist : groupHists) {
        hist->Write();
    }
    outFile->Close();
    std::cout << "Saved channel_groups.root" << std::endl;
    
    // delete canvas;
    for (auto hist : groupHists) {
        delete hist;
    }
}




float get_rms(TH1F *h1)
{

    if (h1->GetEntries() == 0)
        return 0;
    //double mean = h1->GetSum() / h1->GetNbinsX();
    double mean = h1->Integral() / h1->GetNbinsX();
    // cout<<"mean = "<<mean<<" Sum =  "<<h1->GetSum()<<" Integral =  "<<h1->Integral()<<endl;
    //cout<<h1->GetMinimum()<<" "<<h1->GetMinimumBin()<<endl;
    double rms = 0;
    int nbin = h1->GetNbinsX();
    //double sum_test=0;
    for (int j = 0; j != nbin; j++)
    {
     //   sum_test+=h1->GetBinContent(j + 1);
        rms += pow(h1->GetBinContent(j + 1) - mean, 2);
       //cout<<j<<" "<<h1->GetBinContent(j + 1)<<" "<<mean<<" "<<rms<<endl;
    }
    //cout<<"sum_test = "<<sum_test<<endl;
    rms = sqrt(rms / h1->GetNbinsX());

    TH1F h2("h2", "h2", 100, mean - 10 * rms, mean + 10 * rms);
    for (int j = 0; j != nbin; j++)
    {
        if (fabs(h1->GetBinContent(j + 1) - mean) < 6 * rms)
            h2.Fill(h1->GetBinContent(j + 1));
    }
    if (h2.GetEntries() == 0)
        return 0;

    double par[3];
    double xq = 0.5;
    h2.GetQuantiles(1, &par[1], &xq);
    xq = 0.5 + 0.34;
    h2.GetQuantiles(1, &par[0], &xq);
    xq = 0.5 - 0.34;
    h2.GetQuantiles(1, &par[2], &xq);
    mean = par[1];
    rms = std::sqrt((pow(par[0] - par[1], 2) + pow(par[2] - par[1], 2)) / 2.);

    // remove signal
    for (int j = 0; j != nbin; j++)
    {
        if (fabs(h1->GetBinContent(j + 1) - mean) > 3.5 * rms)
        {
            h1->SetBinContent(j + 1, mean);
        }
    }
    h2.Delete();

    return rms;
}

TH1F *get_rms_hist(TH2F *h2)
{
    int Nx = h2->GetNbinsX();
    TString name_ = h2->GetName();
    TH1F *h_rms = new TH1F(name_+"hrms", "RMS", Nx, -0.5, Nx - 0.5);
    for (int i = 1; i <= Nx; i++)
    {
        TH1F *h1 = (TH1F *)h2->ProjectionY("proj", i, i);
        double rms = get_rms(h1);
    //     if(i==4246||i==4245){ 
            
    // //        double rms = get_rms(h1);
    //         cout<<i<<" debug rms = "<<rms<<endl;
    //      }
        h_rms->SetBinContent(i, rms);
    }
    return h_rms;
}



void FillRange(vector<int> &v, int start, int end)
{
    if(start>end){
        int tmp;
        tmp=start;
        start=end;
        end=tmp;
    }
    for (int i = start; i < end; i++)
    {
        v.push_back(i);
    }
           
}

TH1F *CombineFFT(TH2F *hu, int APA = 1, const char *plane = "u", TString tag = "ori", int color = 1)
{

    // 952, 1904, 3072
    vector<int> vchns;
   APA = APA - 1;
    // FillRange(vchns, 1904, 3072); // collection
   if (std::strcmp(plane, "u") == 0)
   {
       FillRange(vchns, APA * 3072, APA * 3072 + 952); // u plane
   }
   else if (std::strcmp(plane, "v") == 0)
   {
       FillRange(vchns, APA * 3072 + 952, APA * 3072 + 1904);
   }
   else if (std::strcmp(plane, "w") == 0)
   {
       FillRange(vchns, APA * 3072 + 1904, APA * 3072 + 3072);
   }else if(std::strcmp(plane, "uv_b_all") == 0)
   {
       FillRange(vchns, 0 * 3072, 0 * 3072 + 952); // u plane
       FillRange(vchns, 0 * 3072 + 952, 0 * 3072 + 1904);
       FillRange(vchns, 1 * 3072, 1 * 3072 + 952); // u plane
       FillRange(vchns, 1 * 3072 + 952, 1 * 3072 + 1904);
   }else if(std::strcmp(plane, "w_b_all") == 0){
       FillRange(vchns, 0 * 3072 + 1904, 0 * 3072 + 3072);
       FillRange(vchns, 1 * 3072 + 1904, 1 * 3072 + 3072);

   }else if(std::strcmp(plane, "uv_t_all") == 0)
   {
       FillRange(vchns, 2 * 3072, 2 * 3072 + 952); // u plane
       FillRange(vchns, 2 * 3072 + 952, 2 * 3072 + 1904);
       FillRange(vchns, 3 * 3072, 3 * 3072 + 952); // u plane
       FillRange(vchns, 3 * 3072 + 952, 3 * 3072 + 1904);
   }else if(std::strcmp(plane, "w_t_all") == 0){
       FillRange(vchns, 2 * 3072 + 1904, 2 * 3072 + 3072);
       FillRange(vchns, 3 * 3072 + 1904, 3 * 3072 + 3072);

   }


    std::cout<<"channel size for fft: "<<vchns.size()<<endl;
    const int nticks = hu->GetNbinsY();
    const int nchs = hu->GetNbinsX();
    TString name_ = hu->GetName();
    // cout<<"nticks = "<<nticks<<endl;
    // exit(0);
    TH1F *hfft = new TH1F(name_+Form("noise_%d%s_", APA + 1, plane) + tag, "noise", nticks, 0, 1. / 0.5); // MHz, 512ns period
    // hfft->SetTitle(Form("FFT -- APA%d %s plane", APA + 1, plane));
    hfft->GetXaxis()->SetTitle("Frequency [MHz]");
    hfft->GetYaxis()->SetTitle("Magnitude [e^{-} / bin]");
    int nfft = 0; // number of non-empty channels
    // TCanvas *c2 = new TCanvas("c2","",800,600);
    //    c2->SaveAs("/nashome/x/xning/Pictures/test.pdf[");
    for (auto channel : vchns)
    {

        TH1F *h2 = new TH1F(Form("Channel%d", channel), Form("Channel %d", channel), nticks, 0, nticks);
        for (int i = 0; i != nticks; i++)
        {
            h2->SetBinContent(i + 1, hu->GetBinContent(channel + 1, i + 1));
        }

        // ExcludeSignal(h2);

        float rms = get_rms(h2);
        TH1F *hmag = (TH1F *)h2->FFT(0, "MAG");
        // cout<<"bin number in hmag : "<<hmag->GetNbinsX()<<endl;
        int dc = hmag->GetBinContent(1); // dc component indicates empty or not
        // if(hmag->GetBinContent(2)<5E3){ // 5E3 avoids the extreme case
        //  if(rms<10 and rms>0){
        if (1){
            nfft++;
            for (int i = 1; i < nticks; i++)
            {
                double content = hfft->GetBinContent(i + 1);
                hfft->SetBinContent(i + 1, content + hmag->GetBinContent(i + 1));
                // cout<<i+1<<"  "<<content + hmag->GetBinContent(i+1)<<endl;
            }
        }
        //        cout<<"channel = "<<channel<<endl;
        //    c2->cd();
        //    hmag->Draw();
        //    c2->SaveAs("/nashome/x/xning/Pictures/test.pdf");
        hmag->Delete();
        h2->Delete();
    }
    // c2->SaveAs("/nashome/x/xning/Pictures/test.pdf]");
    // cout << "nfft= " << nfft << endl;

    hfft->Scale(1. / nfft);
    // ADC -> electrons.  The top drift (plane tags "*_t_*", CRP 2,3) is read
    // out by the TDE; everything else uses the cold-electronics factor that
    // Draw_RMS() selected from the measured RMS.  Linear factor to pair with
    // MAG (amplitude, not power).
    const bool is_top = (TString(plane).Contains("_t_"));
    hfft->Scale(is_top ? ADC2ENC_TOP : ratio_ENC);
    hfft->Rebin(32);
    hfft->GetXaxis()->SetRange(0, 4200);  // X-axis range limited to 1 MHz
    hfft->GetXaxis()->SetRangeUser(0, 1); // X-axis range limited to 1 MHz
    hfft->SetLineColor(color);
    return hfft;
}

TH2F *CovMatrix(TH2F *hu, const char *plane, int APA, TString tag = "ori"){ 
// 952, 1904,
    int ch_i, ch_f;
    APA = APA - 1;
    if (std::strcmp(plane, "u") == 0)
    {
        ch_i = APA * 3072;
        ch_f = APA * 3072 + 952;
    }
    else if (std::strcmp(plane, "v") == 0)
    {
        ch_i = APA * 3072 + 952;
        ch_f = APA * 3072 + 1904;
    }
    else if (std::strcmp(plane, "w") == 0)
    {
        ch_i = APA * 3072 + 1904;
        ch_f = APA * 3072 + 3072;
    }

    gStyle->SetOptStat(0);
    // gStyle->SetPalette(kRainBow);

    int bins = ch_f - ch_i;

    cout << "total number of channels to analyzer: " << bins << endl;

    TPrincipal p(bins, "ND");
    Double_t data[bins];

    // TH2F* h2d = new TH2F("h2d","ch vs ticks",1000, 0, 1000, 4, 0, 4);

    const int nticks = hu->GetNbinsY();
    cout << "n ticks: " << nticks << endl;
    const int nchs = hu->GetNbinsX();
    cout << "n channels: " << nchs << endl;

    for (int i = 0; i != nticks; i++)
    {
        for (int channel = ch_i; channel < ch_f; channel++)
        {
            int bin = channel - ch_i;
            // M[i][channel] = hu->GetBinContent(channel+1,i+1);
            data[bin] = hu->GetBinContent(channel + 1, i + 1);
            if (h_rms->GetBinContent(channel+1)>0 && abs(data[bin])>3.5*h_rms->GetBinContent(channel+1)){
               data[bin] = h_rms->GetBinContent(channel+1); 
            }
        }
        p.AddRow(data);
    }

    const TMatrixD *m = p.GetCovarianceMatrix();
    cout << "n rows = " << m->GetNrows() << endl;
    TMatrixDSym sym;
    sym.Use(m->GetNrows(), m->GetMatrixArray());

    cout << sym(0, 0) << " " << sym(0, 1) << " " << sym(1, 0) << " " << sym(1, 1) << " " << endl;
    // exit(0);
    TH2F *h = new TH2F(Form("CovM_%d%s_", APA + 1, plane) + tag, Form("correlation -- CRP%d %s plane", APA + 1, plane) + tag, bins, ch_i, ch_f, bins, ch_i, ch_f);

    for (int i = 0; i < bins; i++)
    {

        for (int j = 0; j < bins; j++)
        {
            double rho =0;
            if(std::sqrt(sym(i, i) * sym(j, j))!=0)
            rho = sym(i, j) / std::sqrt(sym(i, i) * sym(j, j)); // cov(x,y) = rho * sigma_x * sigma_y,
                                                                       // cout<<"rho = "<<i<<" "<<j<<" "<<sym(i,j)<<" / "<<std::sqrt(sym(i,i) * sym(j,j))<<" =      "<<rho<<endl;
            if (i != j)
            {
                h->SetBinContent(i + 1, j + 1, rho);
                h->SetBinContent(j + 1, i + 1, rho);
                // if (rho == 1)
                // if (i+ch_i>2650 && i+ch_i<2660)
                //     cout << "rho = " << i << " " << j << " " << sym(i, j) << " / " << std::sqrt(sym(i, i) * sym(j, j)) << "  = " << rho << endl;
            }
        }
    }

    return h;
}

void Init(int nticks_input = 6500)
{
    nticks =nticks_input; 
    
h_orig = new TH2F("h_orig", "RawDigits", nchans, -0.5, nchans - 0.5, nticks, 0, nticks);
h_orig_ANf = new TH2F("h_orig_ANf", "RawDigits After Nf", nchans, -0.5, nchans - 0.5, nticks, 0, nticks);
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 3; j++)
        {
            channel_start[i][j] = (APA_number[i] - 1) * 3072 + channels[j];
            channel_end[i][j] = (APA_number[i] - 1) * 3072 + channels[j + 1];
            cout << "channel range: " << channel_start[i][j] << " " << channel_end[i][j] << endl;
            cout << ": " << i << "  " << j << "  " << APA_number[i] << " " << channels[j] << "   " << channels[j + 1] << endl;
            h_orig_sep[i][j] = new TH2F(Form("raw_wf_%d_%s", APA_number[i], plane_name[j]), "", channels[j + 1] - channels[j], channel_start[i][j] - 0.5, channel_end[i][j] - 0.5, nticks, 0, nticks);
            h_orig_ANf_sep[i][j] = new TH2F(Form("raw_wf_ANF_%d_%s", APA_number[i], plane_name[j]), "", channels[j + 1] - channels[j], channel_start[i][j] - 0.5, channel_end[i][j] - 0.5, nticks, 0, nticks);
            h_decon_sep[i][j] = new TH2F(Form("decon_%d_%s", APA_number[i], plane_name[j]), "", channels[j + 1] - channels[j], channel_start[i][j] - 0.5, channel_end[i][j] - 0.5, nticks, 0, nticks);
        } 

    
    const UInt_t Number = 3;
    Double_t Red[Number] = {1.0, 0.0, 1.0};
    Double_t Green[Number] = {1.0, 0.0, 0.0};
    Double_t Blue[Number] = {1.0, 1.0, 0.0};
    Double_t Stops[Number] = {0.0, 0.5, 1.0};

    Double_t Red2[Number] = {0.0, 1.0, 1.0};
    Double_t Green2[Number] = {0.0, 1.0, 0.0};
    Double_t Blue2[Number] = {1.0, 1.0, 0.0};

    Int_t FI = TColor::CreateGradientColorTable(Number, Stops, Red, Green, Blue, nb);
    Int_t FI2 = TColor::CreateGradientColorTable(Number, Stops, Red2, Green2, Blue2, nb);
    for (int i = 0; i < nb; i++)
       { colors[i] = FI + i;
        colors2[i] = FI2 + i;}
    // exit(0);
}

void get_n_bad_channel(TH1F *hist_rms){
    int n_dead_chnl_tmp=0;
    int N = hist_rms->GetNbinsX();
    for(int i=0;i<N;i++){
        if(hist_rms->GetBinContent(i+1)*ratio_ENC>1000){
                n_dead_chnl_tmp++;
                dead_chnl_h.push_back(hist_rms->GetBinCenter(i+1));
        }else if(hist_rms->GetBinContent(i+1)*ratio_ENC<200){
                n_dead_chnl_tmp++;
                dead_chnl_l.push_back(hist_rms->GetBinCenter(i+1));
        }
    }
    n_dead_chnl.push_back(n_dead_chnl_tmp);
}

void Draw_RMS(bool Is_plot=true)
{
    // if(runNo<27977) ratio = ADC2ENC[1];
    //     else ratio =ADC2ENC[0]; 
    c0->cd();
    gPad->SetRightMargin(0.05);
    h_rms = get_rms_hist(h_orig);
    h_rms_ANf = get_rms_hist(h_orig_ANf);
    double tmp_RMS = h_rms_ANf->Integral(1,1000)/1000; 
    if(tmp_RMS<10) {
        gain=7.8;
        ratio_ENC = ADC2ENC[0];
        cout<<"gain 7.8"<<endl;
    }else {
        gain = 11;
        ratio_ENC = ADC2ENC[1];
        cout<<"gain 11"<<endl;
    }
//    ratio_ENC = 1;
    // The top drift (CRP 2,3 = channels >= nchans/2) is read out by the
    // top-drift electronics (TDE), not the cold electronics.  See
    // ADC2ENC_TOP at file scope.  Bottom drift keeps the cold-electronics
    // factor selected above.
    const double ratio_ENC_top = ADC2ENC_TOP; // 69.3 e-/ADC (TDE)

    // bottom part (CRP 0,1 = channels 0 .. nchans/2)
    TH1D* h_rms_induction_b  = new TH1D("h_ind_b","",100,0,1200);
    TH1D* h_rms_collection_b = new TH1D("h_col_b","",100,0,1200);
    TH1D* h_rms_induction_a  = new TH1D("h_ind_a","",100,0,1200);
    TH1D* h_rms_collection_a = new TH1D("h_col_a","",100,0,1200);
    // top part (CRP 2,3 = channels nchans/2 .. nchans): structurally a mirror of
    // the bottom shifted by nchans/2, so reuse the same sub-range logic on k.
    TH1D* h_rms_induction_b_top  = new TH1D("h_ind_b_top","",100,0,1200);
    TH1D* h_rms_collection_b_top = new TH1D("h_col_b_top","",100,0,1200);
    TH1D* h_rms_induction_a_top  = new TH1D("h_ind_a_top","",100,0,1200);
    TH1D* h_rms_collection_a_top = new TH1D("h_col_a_top","",100,0,1200);
    for (int i=0;i<nchans/2;i++){
       if(
        (i>97&&i<380 || i>573 && i<856) || (i>97+951&&i<380+951 || i>573+951 && i<856+951) ||
        (i>97+3069&&i<380+3069 || i>573+3069 && i<856+3069) || (i>97+951+3069&&i<380+951+3069 || i>573+951+3069 && i<856+951+3069)
        )
       {
            h_rms_induction_b->Fill(h_rms->GetBinContent(i)*ratio_ENC);
            h_rms_induction_a->Fill(h_rms_ANf->GetBinContent(i)*ratio_ENC);
        }else if(
            i>951*2 && i<1167+951*2 ||
            i>951*2+ 3069 && i<1167+951*2+ 3069
        ){
            h_rms_collection_b->Fill(h_rms->GetBinContent(i)*ratio_ENC);
            h_rms_collection_a->Fill(h_rms_ANf->GetBinContent(i)*ratio_ENC);
        }
    }
    // top: identical plane selection on k = channel - nchans/2
    for (int i=nchans/2;i<nchans;i++){
       int k = i - nchans/2;
       if(
        (k>97&&k<380 || k>573 && k<856) || (k>97+951&&k<380+951 || k>573+951 && k<856+951) ||
        (k>97+3069&&k<380+3069 || k>573+3069 && k<856+3069) || (k>97+951+3069&&k<380+951+3069 || k>573+951+3069 && k<856+951+3069)
        )
       {
            h_rms_induction_b_top->Fill(h_rms->GetBinContent(i)*ratio_ENC_top);
            h_rms_induction_a_top->Fill(h_rms_ANf->GetBinContent(i)*ratio_ENC_top);
        }else if(
            k>951*2 && k<1167+951*2 ||
            k>951*2+ 3069 && k<1167+951*2+ 3069
        ){
            h_rms_collection_b_top->Fill(h_rms->GetBinContent(i)*ratio_ENC_top);
            h_rms_collection_a_top->Fill(h_rms_ANf->GetBinContent(i)*ratio_ENC_top);
        }
    }

    // ratio_ENC=1;
    get_n_bad_channel(h_rms_ANf); 
    cout<<"n_dead_chnl = "<<n_dead_chnl.back()<<endl;
    mean_RMS.push_back(h_rms->Integral()/h_rms->GetNbinsX()*ratio_ENC); 
    mean_RMS_AfN.push_back(h_rms_ANf->Integral()/h_rms_ANf->GetNbinsX()*ratio_ENC); 


    if(Is_plot){
    // Drop the 0-12 e- bin from the filtered curves (see drop_first_bin).
    // Must precede the SetMaximum calls below, which scale the headroom off
    // GetMaximum() and would otherwise still size the pad to the spike.
    drop_first_bin(h_rms_induction_a);
    drop_first_bin(h_rms_collection_a);
    drop_first_bin(h_rms_induction_a_top);
    drop_first_bin(h_rms_collection_a_top);
    plot_style();
    plot_pad();
    // Per-channel noise vs channel number (diagnostic, not a final figure).
    h_rms->Draw("hist");
    h_rms->GetYaxis()->SetRangeUser(0, 50);
    h_rms->GetXaxis()->SetTitle("Channel number");
    h_rms->GetYaxis()->SetTitle("Noise RMS [ADC]");
    plot_axes(h_rms);
    h_rms->SetLineColor(4);
    h_rms_ANf->SetLineColor(2);
    h_rms_ANf->Draw("same hist");
    // Legend text is kept to the two words that distinguish the curves; the
    // detector/plane context lives in the in-frame label.
    TLegend *leg = plot_legend(0.60, 0.72, 0.93, 0.90);
    leg->AddEntry(h_rms, "Raw", "l");
    leg->AddEntry(h_rms_ANf, "Filtered", "l");
    leg->Draw();
    c0->SaveAs(outname + Form("rms_%d_%d.png", runNo, evtNo));

    // The ENC panels below draw *different* histograms (the per-plane ENC
    // distributions), so each needs its own legend built from the objects
    // actually drawn -- otherwise the swatch colours do not match the curves.

    // Start from an empty pad: leftover "same hist" curves from the
    // previous panel would otherwise be redrawn on top of this one.
    c0->Clear();
    plot_pad();
    h_rms_induction_a->GetXaxis()->SetTitle("Noise [e^{-} ENC]");
    h_rms_induction_a->GetYaxis()->SetTitle("Channels");
    plot_axes(h_rms_induction_a);
    // Headroom so the in-frame label and legend clear the peak.
    h_rms_induction_a->SetMaximum(1.45 * TMath::Max(h_rms_induction_a->GetMaximum(), h_rms_induction_b->GetMaximum()));
    h_rms_induction_a->SetLineColor(2);
    h_rms_induction_b->SetLineColor(4);
    h_rms_induction_a->Draw("hist");
    plot_axes(h_rms_induction_b);
    h_rms_induction_b->Draw("same hist");
    plot_label("VD bottom, induction");
    TLegend *leg_ind = plot_legend(0.60, 0.72, 0.93, 0.90);
    leg_ind->AddEntry(h_rms_induction_b, "Raw", "l");
    leg_ind->AddEntry(h_rms_induction_a, "Filtered", "l");
    leg_ind->Draw();
    c0->SaveAs(outname + Form("rms_ind_%d_%d.png", runNo, evtNo));

    // Start from an empty pad: leftover "same hist" curves from the
    // previous panel would otherwise be redrawn on top of this one.
    c0->Clear();
    plot_pad();
    h_rms_collection_a->GetXaxis()->SetTitle("Noise [e^{-} ENC]");
    h_rms_collection_a->GetYaxis()->SetTitle("Channels");
    plot_axes(h_rms_collection_a);
    // Headroom so the in-frame label and legend clear the peak.
    h_rms_collection_a->SetMaximum(1.45 * TMath::Max(h_rms_collection_a->GetMaximum(), h_rms_collection_b->GetMaximum()));
    h_rms_collection_a->SetLineColor(2);
    h_rms_collection_b->SetLineColor(4);
    h_rms_collection_a->Draw("hist");
    plot_axes(h_rms_collection_b);
    h_rms_collection_b->Draw("same hist");
    plot_label("VD bottom, collection");
    TLegend *leg_col = plot_legend(0.60, 0.72, 0.93, 0.90);
    leg_col->AddEntry(h_rms_collection_b, "Raw", "l");
    leg_col->AddEntry(h_rms_collection_a, "Filtered", "l");
    leg_col->Draw();
    c0->SaveAs(outname + Form("rms_col_%d_%d.png", runNo, evtNo));

    // ---- top part (CRP 2,3) ----
    // top-drift histograms sit at high ENC (peaks ~700-900 e-), so the
    // left side of the frame is empty: put legend and label there.
    TLegend *leg_top = plot_legend(0.20, 0.72, 0.53, 0.90);
    leg_top->AddEntry(h_rms, "Raw", "l");
    leg_top->AddEntry(h_rms_ANf, "Filtered", "l");

    // Start from an empty pad: leftover "same hist" curves from the
    // previous panel would otherwise be redrawn on top of this one.
    c0->Clear();
    plot_pad();
    h_rms_induction_a_top->GetXaxis()->SetTitle("Noise [e^{-} ENC]");
    h_rms_induction_a_top->GetYaxis()->SetTitle("Channels");
    plot_axes(h_rms_induction_a_top);
    // Headroom so the in-frame label and legend clear the peak.
    h_rms_induction_a_top->SetMaximum(1.45 * TMath::Max(h_rms_induction_a_top->GetMaximum(), h_rms_induction_b_top->GetMaximum()));
    h_rms_induction_a_top->SetLineColor(2);
    h_rms_induction_b_top->SetLineColor(4);
    h_rms_induction_a_top->Draw("hist");
    plot_axes(h_rms_induction_b_top);
    h_rms_induction_b_top->Draw("same hist");
    plot_label("VD top, induction", 0.50);
    TLegend *leg_ind_top = plot_legend(0.20, 0.72, 0.53, 0.90);
    leg_ind_top->AddEntry(h_rms_induction_b_top, "Raw", "l");
    leg_ind_top->AddEntry(h_rms_induction_a_top, "Filtered", "l");
    leg_ind_top->Draw();
    c0->SaveAs(outname + Form("rms_ind_top_%d_%d.png", runNo, evtNo));

    // Start from an empty pad: leftover "same hist" curves from the
    // previous panel would otherwise be redrawn on top of this one.
    c0->Clear();
    plot_pad();
    h_rms_collection_a_top->GetXaxis()->SetTitle("Noise [e^{-} ENC]");
    h_rms_collection_a_top->GetYaxis()->SetTitle("Channels");
    plot_axes(h_rms_collection_a_top);
    // Headroom so the in-frame label and legend clear the peak.
    h_rms_collection_a_top->SetMaximum(1.45 * TMath::Max(h_rms_collection_a_top->GetMaximum(), h_rms_collection_b_top->GetMaximum()));
    h_rms_collection_a_top->SetLineColor(2);
    h_rms_collection_b_top->SetLineColor(4);
    h_rms_collection_a_top->Draw("hist");
    plot_axes(h_rms_collection_b_top);
    h_rms_collection_b_top->Draw("same hist");
    plot_label("VD top, collection", 0.50);
    TLegend *leg_col_top = plot_legend(0.20, 0.72, 0.53, 0.90);
    leg_col_top->AddEntry(h_rms_collection_b_top, "Raw", "l");
    leg_col_top->AddEntry(h_rms_collection_a_top, "Filtered", "l");
    leg_col_top->Draw();
    c0->SaveAs(outname + Form("rms_col_top_%d_%d.png", runNo, evtNo));
    }

    //h_rms->Delete();
    //h_rms_ANf->Delete();
}

void RMS_bad_channel()
{
    h_rms_ANf = get_rms_hist(h_orig_ANf);
    double tmp_RMS = h_rms_ANf->Integral(1,1000)/1000; 
    if(tmp_RMS<10) {
        gain=7.8;
        ratio_ENC = ADC2ENC[0];
    }else {
        gain = 14;
        ratio_ENC = ADC2ENC[1];
    }

    get_n_bad_channel(h_rms_ANf); 

}

void Draw_wf()
{
    // gStyle->SetPalette(kLightTemperature);
    c0->cd();
    gPad->SetRightMargin(0.15);
    h_orig->Draw("colz");
    h_orig->GetZaxis()->SetRangeUser(-150, 150);
    h_orig->GetXaxis()->SetTitle("Channel number");
    h_orig->GetYaxis()->SetTitle("nticks");
    h_orig->GetZaxis()->SetTitle("ADC");
    h_orig->SetTitle("Raw waveform at " + time_string);
    c0->SaveAs(outname + Form("raw_%d_%d.png", runNo, evtNo));

    h_orig_ANf->Draw("colz");
    h_orig_ANf->GetZaxis()->SetRangeUser(-150, 150);
    h_orig_ANf->GetXaxis()->SetTitle("Channel number");
    h_orig_ANf->GetYaxis()->SetTitle("nticks");
    h_orig_ANf->GetZaxis()->SetTitle("ADC");
    h_orig_ANf->SetTitle("After Nf at " + time_string);
    c0->SaveAs(outname + Form("raw_ANf_%d_%d.png", runNo, evtNo));
}

void Draw_wf_sep()
{
    // gStyle->SetPalette(kLightTemperature);
    gStyle->SetPalette(nb, colors2);
    c0->cd();
    gPad->SetRightMargin(0.15);
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 2; j++)
        {
            h_orig_sep[i][j]->Draw("colz");
            h_orig_sep[i][j]->GetZaxis()->SetRangeUser(-150, 150);
            h_orig_sep[i][j]->GetXaxis()->SetTitle("Channel number");
            h_orig_sep[i][j]->GetYaxis()->SetTitle("nticks");
            h_orig_sep[i][j]->GetZaxis()->SetTitle("ADC");
            h_orig_sep[i][j]->SetTitle(Form("Raw waveform CRP%d_%s_%d_%d ", APA_number[i], plane_name[j], runNo, evtNo));
            c0->SaveAs(outname + Form("raw_apa%d_%s_%d_%d.png", APA_number[i], plane_name[j], runNo, evtNo));

            h_orig_ANf_sep[i][j]->Draw("colz");
            h_orig_ANf_sep[i][j]->GetZaxis()->SetRangeUser(-150, 150);
            h_orig_ANf_sep[i][j]->GetXaxis()->SetTitle("Channel number");
            h_orig_ANf_sep[i][j]->GetYaxis()->SetTitle("nticks");
            h_orig_ANf_sep[i][j]->GetZaxis()->SetTitle("ADC");
            h_orig_ANf_sep[i][j]->SetTitle(Form("Raw waveform ANf CRP%d_%s_%d_%d ", APA_number[i], plane_name[j], runNo, evtNo));
            c0->SaveAs(outname + Form("raw_ANf_apa%d_%s_%d_%d.png", APA_number[i], plane_name[j], runNo, evtNo));
        }

    gStyle->SetPalette(nb, colors);
    for (int i = 0; i < 4; i++)
        for (int j = 2; j < 3; j++)
        {
            h_orig_sep[i][j]->Draw("colz");
            h_orig_sep[i][j]->GetZaxis()->SetRangeUser(-10, 150);
            h_orig_sep[i][j]->GetXaxis()->SetTitle("Channel number");
            h_orig_sep[i][j]->GetYaxis()->SetTitle("nticks");
            h_orig_sep[i][j]->GetZaxis()->SetTitle("ADC");
            h_orig_sep[i][j]->SetTitle(Form("Raw waveform CRP%d_%s_%d_%d ", APA_number[i], plane_name[j], runNo, evtNo));
            c0->SaveAs(outname + Form("raw_apa%d_%s_%d_%d.png", APA_number[i], plane_name[j], runNo, evtNo));

            h_orig_ANf_sep[i][j]->Draw("colz");
            h_orig_ANf_sep[i][j]->GetZaxis()->SetRangeUser(-10, 150);
            h_orig_ANf_sep[i][j]->GetXaxis()->SetTitle("Channel number");
            h_orig_ANf_sep[i][j]->GetYaxis()->SetTitle("nticks");
            h_orig_ANf_sep[i][j]->GetZaxis()->SetTitle("ADC");
            h_orig_ANf_sep[i][j]->SetTitle(Form("Raw waveform ANf CRP%d_%s_%d_%d ", APA_number[i], plane_name[j], runNo, evtNo));
            c0->SaveAs(outname + Form("raw_ANf_apa%d_%s_%d_%d.png", APA_number[i], plane_name[j], runNo, evtNo));
        }
TFile *f1  =  new TFile(outname+"test_vd_1event.root","recreate");
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 3; j++){
            h_orig_sep[i][j]->Write();
            h_orig_ANf_sep[i][j]->Write();
            h_decon_sep[i][j]->Write();
    }
f1->Close();

}

void Draw_decon_sep()
{
    // gStyle->SetPalette(kLightTemperature);
    gStyle->SetPalette(nb, colors2);
    c0->cd();
    gPad->SetRightMargin(0.15);
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 2; j++)
        {
            h_decon_sep[i][j]->Draw("colz");
            h_decon_sep[i][j]->GetZaxis()->SetRangeUser(0, 20);
            h_decon_sep[i][j]->GetXaxis()->SetTitle("Channel number");
            h_decon_sep[i][j]->GetYaxis()->SetTitle("nticks");
            h_decon_sep[i][j]->GetZaxis()->SetTitle("1/1000 charge");
            h_decon_sep[i][j]->SetTitle(Form("Decon waveform CRP%d_%s_%d_%d ", APA_number[i], plane_name[j], runNo, evtNo));
            c0->SaveAs(outname + Form("decon_apa%d_%s_%d_%d.png", APA_number[i], plane_name[j], runNo, evtNo));
     
        }

    gStyle->SetPalette(nb, colors);
    for (int i = 0; i < 4; i++)
        for (int j = 2; j < 3; j++)
        {
            h_decon_sep[i][j]->Draw("colz");
            h_decon_sep[i][j]->GetZaxis()->SetRangeUser(0, 20);
            h_decon_sep[i][j]->GetXaxis()->SetTitle("Channel number");
            h_decon_sep[i][j]->GetYaxis()->SetTitle("nticks");
            h_decon_sep[i][j]->GetZaxis()->SetTitle("1/1000 charge");
            h_decon_sep[i][j]->SetTitle(Form("Decon waveform CRP%d_%s_%d_%d ", APA_number[i], plane_name[j], runNo, evtNo));
            c0->SaveAs(outname + Form("decon_apa%d_%s_%d_%d.png", APA_number[i], plane_name[j], runNo, evtNo));

        }
}

void Draw_baseline()
{
    c0->cd();
    gPad->SetRightMargin(0.05);
    gStyle->SetPadRightMargin(0.08);
    h_baseline->Draw();
    h_baseline->GetXaxis()->SetTitle("Channel number");
    h_baseline->GetYaxis()->SetTitle("Baseline[ADC]");
    h_baseline->Draw();
    c0->SaveAs(outname + Form("baseline_%d_%d.png", runNo, evtNo));
}

void Draw_Hit(){
    c0->cd();
    gPad->SetRightMargin(0.05);
    gStyle->SetPadRightMargin(0.08);
    h_nHit->Draw();
    h_nHit->GetXaxis()->SetTitle("Channel number");
    h_nHit->GetYaxis()->SetTitle("# of Hits");
    h_nHit->Draw();
    c0->SaveAs(outname + Form("nHits_%d_%d.png", runNo, evtNo));

    c0->cd();
    gPad->SetLeftMargin(0.15);
    h_nHit_ADC->Draw();
    h_nHit_ADC->GetXaxis()->SetTitle("Channel number");
    h_nHit_ADC->GetYaxis()->SetTitle("Total ADC of Hits");
    h_nHit_ADC->GetYaxis()->SetTitleOffset(1.6);
    h_nHit_ADC->Draw("hist");
    c0->SaveAs(outname + Form("Hits_ADC_%d_%d.png", runNo, evtNo));

}

void Draw_fft()
{
    c0->cd();
    gPad->SetRightMargin(0.05);
    gStyle->SetPadRightMargin(0.08);
    TLegend *leg = new TLegend(0.65, 0.75, 0.87, 0.87);
    leg->SetBorderSize(0);
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 3; j++)
        {
            hfft[i][j] = CombineFFT(h_orig, APA_number[i], plane_name[j]);
            hfft_ANf[i][j] = CombineFFT(h_orig_ANf, APA_number[i], plane_name[j]);
            noise_spectra_peak[i][j] =hfft_ANf[i][j]->GetMaximum(); 
            hfft[i][j]->Draw("hist");
            hfft_ANf[i][j]->SetLineColor(2);
            hfft_ANf[i][j]->Draw("same hist");
            // f1->cd();
			// hfft_ANf[i][j]->Write();	
            if (i == 0 && j == 0)
            {
                leg->AddEntry(hfft[0][0], " Before Noise Filter", "l");
                leg->AddEntry(hfft_ANf[0][0], "After Noise Filter", "l");
                cout<<"test fft integral "<<hfft[i][j]->Integral()<<endl;
            }
            leg->Draw();
            c0->SaveAs(outname + Form("fft%d%s_%d_%d.png", APA_number[i], plane_name[j], runNo, evtNo));
        }
    noise_spectra_peak_all.push_back(noise_spectra_peak);
}

void Draw_fft_ic(){

    hfft_ic[0][0] = CombineFFT(h_orig, 12, "uv_b_all");
    hfft_ic_ANf[0][0] = CombineFFT(h_orig_ANf, 12, "uv_b_all");
    hfft_ic[0][1] = CombineFFT(h_orig, 12, "w_b_all");
    hfft_ic_ANf[0][1] = CombineFFT(h_orig_ANf, 12, "w_b_all");

    hfft_ic[1][0] = CombineFFT(h_orig, 34, "uv_t_all");
    hfft_ic_ANf[1][0] = CombineFFT(h_orig_ANf, 34, "uv_t_all");
    hfft_ic[1][1] = CombineFFT(h_orig, 34, "w_t_all");
    hfft_ic_ANf[1][1] = CombineFFT(h_orig_ANf, 34, "w_t_all");
    // In-frame labels rather than pad titles.
    TString hist_label[2][2] = {{"VD bottom, induction", "VD bottom, collection"},
                                {"VD top, induction", "VD top, collection"}};

    plot_style();
    plot_pad();
    c0->cd();
    TLegend *leg = plot_legend(0.60, 0.72, 0.93, 0.90);
    for(int i=0;i<2;i++)
    for(int j=0;j<2;j++){
        // Empty pad per panel, as in Draw_RMS.
        c0->Clear();
        plot_pad();
        hfft_ic[i][j]->SetLineColor(4);
        hfft_ic_ANf[i][j]->SetLineColor(2);
        plot_axes(hfft_ic[i][j]);
        // Headroom so the in-frame label and legend clear the spectrum.
        hfft_ic[i][j]->SetMaximum(1.45 * TMath::Max(hfft_ic[i][j]->GetMaximum(),
                                                    hfft_ic_ANf[i][j]->GetMaximum()));
        hfft_ic[i][j]->Draw("hist");
        hfft_ic_ANf[i][j]->Draw("same hist");
            if (i == 0 && j == 0)
            {
                leg->AddEntry(hfft_ic[0][0], "Raw", "l");
                leg->AddEntry(hfft_ic_ANf[0][0], "Filtered", "l");
            }
        leg->Draw();
        plot_label(hist_label[i][j]);
        c0->SaveAs(outname + Form("fft_%d_%d.png",i,j));
    }

}


void Draw_cov()
{
    c0->cd();
    gPad->SetRightMargin(0.15);
    gStyle->SetPalette(kLightTemperature);
    // gStyle->SetPalette(nb, colors);
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 3; j++)
        {
            h_cov[i][j] = CovMatrix(h_orig, plane_name[j], APA_number[i]);
            h_cov[i][j]->GetXaxis()->SetTitle("Channel number");
            h_cov[i][j]->GetYaxis()->SetTitle("Channel number");
            h_cov[i][j]->GetZaxis()->SetTitle("Correlation coefficient");
            h_cov[i][j]->GetZaxis()->SetRangeUser(-0.3, 0.5);
            h_cov[i][j]->Draw("colz");
            c0->SaveAs(outname + Form("covMatx_%d%s_%d_%d.png", APA_number[i], plane_name[j], runNo, evtNo));

            h_cov_ANf[i][j] = CovMatrix(h_orig_ANf, plane_name[j], APA_number[i], "ANf");
            h_cov_ANf[i][j]->GetXaxis()->SetTitle("Channel number");
            h_cov_ANf[i][j]->GetYaxis()->SetTitle("Channel number");
            h_cov_ANf[i][j]->GetZaxis()->SetTitle("Correlation coefficient");
            h_cov_ANf[i][j]->GetZaxis()->SetRangeUser(-0.3, 0.5);
            h_cov_ANf[i][j]->Draw("colz");
            c0->SaveAs(outname + Form("cov_ANfMatx_%d%s_%d_%d.png", APA_number[i], plane_name[j], runNo, evtNo));
        }
}


