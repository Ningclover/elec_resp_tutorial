#include "all.h"

using namespace art;
using namespace std;
using namespace std::chrono;

void convert_artroot_DQM(std::string const &filename, TString outpath = "./NF_plots")
{
    gStyle->SetOptStat(0);
    // gStyle->SetPalette(kLightTemperature);
    InputTag rawdigits_tag{"tpcrawdecoder:daq"};
    InputTag AftNoise_tag{"wclsdatavdnf:raw"};
    InputTag reco_tag{"wclsdatavd:gauss"};


    // Create a vector of length 1, containing the given filename.
    vector<string> filenames(1, filename);
    int n = 0;

    for (gallery::Event ev(filenames); !ev.atEnd(); ev.next())
    {

        auto aux = ev.eventAuxiliary();
        std::cout << "processing event: " << aux.run() << "-" << aux.event() << "  "<<n<<std::endl;
        // continue;

            // continue;
        //  if (n!=2) continue;
        // nf_double_check: NF artROOT has only the first event -> process n==0
         if (n!=0) {
            n++;
            continue;
         }

        // std::cout <<"time  "<<aux.time().value()<<"  "<<aux.time().timeLow()<<"  "<<aux.time().timeHigh()<<std::endl;
        auto t1 = TTimeStamp(aux.time().timeHigh(), aux.time().timeLow());
        time_string = t1.AsString();
        std::cout << t1.AsString() << std::endl;

        runNo = aux.run();
        evtNo = aux.event();
        TString path = outpath+Form("/run_%d/evt_%d/",runNo,evtNo);
        gSystem->mkdir(path.Data(),true);
        outname=path;
        evtNo_list.push_back(evtNo);
        auto const &rawdigits = *ev.getValidHandle<vector<raw::RawDigit>>(rawdigits_tag);
        auto const &AftNoise = *ev.getValidHandle<vector<raw::RawDigit>>(AftNoise_tag);
        // auto const &reco = *ev.getValidHandle<vector<recob::Wire>>(reco_tag);

        if (rawdigits.empty())
        {
            std::cout << "WARNING: no RawDigit found." << std::endl;
            return;
        }
        int nsample =rawdigits.begin()->Samples(); 
        cout<<"nsample = "<<nsample<<endl;
        Init(nsample);
        gStyle->SetPalette(nb, colors2);

                for (auto rd : rawdigits)
        {
//            if (rd.Channel() == 0)
//                std::cout << rd.Samples() << std::endl;
//                   std::cout << "channel: " << rd.Channel()
//                             << " NADC: " << rd.NADC()
//                             << " Samples: " << rd.Samples()
//                             << " Pedestal: " << rd.GetPedestal()
//                             << " Sigma: " << rd.GetSigma()
//                             << std::endl;


            int channel = rd.Channel();

            h_baseline->SetBinContent(channel + 1, rd.GetPedestal());

            // h_rms->SetBinContent(channel+1, rd.GetSigma());

            int nSamples = rd.Samples();
            for (int j = 0; j < nSamples; j++)
            {
                h_orig->SetBinContent(channel + 1, j + 1, rd.ADC(j) - rd.GetPedestal());
                for(int k=0;k<4;k++)
                    for(int l=0;l<3;l++){
                        if(channel>=channel_start[k][l]&&channel<channel_end[k][l]){
                            h_orig_sep[k][l]->SetBinContent(channel + 1-channel_start[k][l],j + 1, rd.ADC(j) - rd.GetPedestal());
                            h_orig_sep[k][l]->SetTitle(Form("raw_wf_run_%d_evt_%d_APA%d_%s",runNo,evtNo,APA_number[k],plane_name[l]));
                        }
                }


            }

        } // end of rawdigits

        if (AftNoise.empty())
        {
            std::cout << "WARNING: no wclsdatahdfilter:raw found." << std::endl;

            // return;
            
        }else{
        for (auto rd : AftNoise)
        {
            //    std::cout << "channel: " << rd.Channel()
            //              << " NADC: " << rd.NADC()
            //              << " Samples: " << rd.Samples()
            //              << " Pedestal: " << rd.GetPedestal()
            //              << " Sigma: " << rd.GetSigma()
            //              << std::endl;


            int channel = rd.Channel();

            int nSamples = rd.Samples();
            for (int j = 0; j < nSamples; j++)
            {
                h_orig_ANf->SetBinContent(channel + 1, j + 1, rd.ADC(j) - rd.GetPedestal());
                    for(int k=0;k<4;k++)
                        for(int l=0;l<3;l++){
                        if(channel>channel_start[k][l]&&channel<channel_end[k][l]){
                            h_orig_ANf_sep[k][l]->SetBinContent(channel + 1-channel_start[k][l],j + 1, rd.ADC(j) - rd.GetPedestal());
                            h_orig_ANf_sep[k][l]->SetTitle(Form("raw_wf_ANf_run_%d_evt_%d_APA%d_%s",runNo,evtNo,APA_number[k],plane_name[l]));
                        }
                    }
            }
        } // end of AftNoise digits
    }

        // if (reco.empty())
        // {
        //     std::cout << "WARNING: no reco found." << std::endl;

        //     // return;
            
        // }else{
        //         for (auto wire : reco)
        //     {
        //     int channel = wire.Channel();
        //     int nSamples = wire.NSignal();
        //     std::vector<float> signal = wire.Signal();
        //     // h_rms->SetBinContent(channel+1, rd.GetSigma());

        //     for (int j = 0; j < nSamples; j++)
        //     {
        //         for(int k=0;k<4;k++)
        //             for(int l=0;l<3;l++){
        //                 if(channel>=channel_start[k][l]&&channel<channel_end[k][l]){
        //                     h_decon_sep[k][l]->SetBinContent(channel + 1-channel_start[k][l],j + 1, signal[j]);
        //                     h_decon_sep[k][l]->SetTitle(Form("decon_run_%d_evt_%d_APA%d_%s",runNo,evtNo,APA_number[k],plane_name[l]));
        //                 }
        //         }

        //     }

        //     } 

        // }


     //   Draw_wf();
        Draw_wf_sep();
        // Draw_decon_sep();
        // processChannelGroups(h_orig);
        // processChannelGroups(h_orig_ANf,"anf");
    //    Draw_RMS();
     //   Draw_baseline();
    //    Draw_fft();
    //   Draw_cov();
        Draw_RMS();
       Draw_fft_ic();
        n++;
    }
    // c1->SaveAs("/nashome/x/xning/Pictures/outfile.pdf]");
//     ofstream fout;
//     //TString name = Form("/nashome/x/xning/runinfo/runNo%d.dat",runNo);
//     TString name = Form("/nashome/x/xning/runinfo/new/runNo%d.dat",runNo);
//     fout.open(name.Data(),std::ios_base::app);
//     //fout.open(name.Data());
//     for(auto event : evtNo_list){
//         fout<<runNo<<"\t"<<event<<endl;
//     }
//     fout.close();

//     ofstream fout2;
//     //TString name = Form("/nashome/x/xning/runinfo/runNo%d.dat",runNo);
//     TString name2 = Form("/exp/dune/data/users/xning/info/runNo%d.dat",runNo);
//     fout2.open(name2.Data(),std::ios_base::app);
//     //fout.open(name.Data());
//     int k=0;
//     for(auto event : evtNo_list){
//         fout2<<runNo<<"\t"<<event<<"\t"<<mean_RMS_AfN[k]<<"\t";
//         cout<<runNo<<"\t"<<event<<"\t"<<mean_RMS_AfN[k]<<"\t";
//            for (int i = 0; i < 4; i++)
//                for (int j = 0; j < 3; j++){
//                    fout2<<noise_spectra_peak_all[k][i][j]<<"\t";
//                    cout<<noise_spectra_peak_all[k][i][j]<<"\t";
//                }
//         
//         
//         fout2<<endl;
//         cout<<endl;
//         k++;
//     }
//     fout2.close();


}
