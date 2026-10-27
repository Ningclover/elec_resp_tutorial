#include <chrono>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

#include "lardataobj/Utilities/sparse_vector.h"
#include "canvas/Utilities/InputTag.h"
#include "gallery/Event.h"
#include "gallery/Handle.h"

#include "lardataobj/RawData/RawDigit.h"

#include "TFile.h"
#include "TH1F.h"
#include "TInterpreter.h"
#include "TROOT.h"

using namespace art;
using namespace std;
using namespace std::chrono;

void
convert_artroot_raw(std::string const& filename)
{
  // PDHD per-channel-corrected RawDigit product, written by the calibration NF.
  // The eleccalib file holds several "wclsdatahdfilter:raw" products under
  // different process names (corrected pdhdeleccalib / pdhdeleccalibh5, plus the
  // upstream uncorrected pdhdkeepupstage1).  Prefer the corrected one; a bare
  // "wclsdatahdfilter:raw" is ambiguous across processes.
  std::vector<InputTag> candidate_tags{
    InputTag{ "wclsdatahdfilter:raw:pdhdeleccalib" },
    InputTag{ "wclsdatahdfilter:raw:pdhdeleccalibh5" },
    InputTag{ "wclsdatahdfilter:raw:pdhdkeepupstage1" },
  };

  // Create a vector of length 1, containing the given filename.
  vector<string> filenames(1, filename);

  // Don't do the following in compiled C++. This code relies on the
  // interactive ROOT system to (implicitly) own the histograms we
  // create on the heap. In a C++ program, this object would be leaked
  // (the memory unrecoverable by the program).

  const int nticks = 8192;
  auto h_raw = new TH2I("h_raw", "RawDigits", 10240, -0.5, 10239.5, nticks, 0, nticks);
  auto h_baseline = new TH1I("h_baseline", "Pedestal", 10240, -0.5, 10239.5);

  int n = 0;
  for (gallery::Event ev(filenames); !ev.atEnd(); ev.next()) {

    if (n>0) break;
    // std::cout << "processing event: " << n << std::endl;
    auto aux = ev.eventAuxiliary();
    std::cout << "processing event: " << aux.run() << "-" << aux.event() << std::endl;

    // Pick the first candidate tag that yields a non-empty RawDigit collection.
    InputTag rawdigits_tag;
    gallery::Handle<vector<raw::RawDigit>> rawdigits_handle;
    bool found = false;
    for (auto const& tag : candidate_tags) {
      gallery::Handle<vector<raw::RawDigit>> h;
      if (ev.getByLabel(tag, h) && h.isValid() && !h->empty()) {
        rawdigits_tag = tag; rawdigits_handle = h; found = true; break;
      }
    }
    if (!found) {
      std::cout << "ERROR: no non-empty wclsdatahdfilter:raw product found." << std::endl;
      return;
    }
    std::cout << "reading RawDigits from tag: " << rawdigits_tag.encode() << std::endl;

    auto const& rawdigits = *rawdigits_handle;
    
    
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
         h_raw->SetBinContent(channel+1, j+1, rd.ADC(j) - rd.GetPedestal());
      }
    } // end of rawdigits


    auto ofile = TFile::Open(Form("hraw-magnify-%d-%d.root", aux.run(), aux.event()),"recreate");
    h_raw->Write();
    h_baseline->Write();
    ofile->Close();

    n++;
  }

  // hsigma->Draw();

}
