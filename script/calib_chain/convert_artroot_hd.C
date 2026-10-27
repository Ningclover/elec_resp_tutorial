#include <chrono>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

#include "canvas/Utilities/InputTag.h"
#include "gallery/Event.h"

#include "TFile.h"
#include "TH1F.h"
#include "TInterpreter.h"
#include "TROOT.h"

using namespace art;
using namespace std;
using namespace std::chrono;

void
convert_artroot_hd(std::string const& filename)
{
  InputTag rawdigits_tag{ "tpcrawdecoder:daq" };

  // Create a vector of length 1, containing the given filename.
  vector<string> filenames(1, filename);

  // Don't do the following in compiled C++. This code relies on the
  // interactive ROOT system to (implicitly) own the histograms we
  // create on the heap. In a C++ program, this object would be leaked
  // (the memory unrecoverable by the program).

  const int nticks = 8192;
  auto h_daq = new TH2I("h_daq", "RawDigits", 10240, -0.5, 10239.5, nticks, 0, nticks);
  auto h_baseline = new TH1I("h_baseline", "Pedestal", 10240, -0.5, 10239.5);

  int n = 0;
  for (gallery::Event ev(filenames); !ev.atEnd(); ev.next()) {

    if (n>0) break;
    // std::cout << "processing event: " << n << std::endl;
    auto aux = ev.eventAuxiliary();
    std::cout << "processing event: " << aux.run() << "-" << aux.event() << std::endl;

    auto const& rawdigits = *ev.getValidHandle<vector<raw::RawDigit>>(rawdigits_tag);

    if (rawdigits.empty()) {
      std::cout << "WARNING: no RawDigit found." << std::endl;
      return;
    }
    
    for (auto rd: rawdigits) {
      // std::cout << "channel: " << rd.Channel()
      //           << " NADC: " << rd.NADC()
      //           << " Samples: " << rd.Samples()
      //           << " Pedestal: " << rd.GetPedestal()
      //           << " Sigma: " << rd.GetSigma()
      //           << std::endl;

      int channel = rd.Channel();
      // std::cout << "channel: " << channel << " threshold: " << thresholds.at(channel) << std::endl;
      h_baseline->SetBinContent(channel+1, rd.GetPedestal());

      int nSamples = rd.Samples();
      for (int j = 0; j < nSamples; j++) {
         h_daq->SetBinContent(channel+1, j+1, rd.ADC(j) - rd.GetPedestal());
      }
    } // end of rawdigits


    auto ofile = TFile::Open(Form("magnify-%d-%d.root", aux.run(), aux.event()),"recreate");
    h_daq->Write();
    h_baseline->Write();
    ofile->Close();

    n++;
  }

  // hsigma->Draw();

}
