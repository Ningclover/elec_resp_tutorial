// Read raw::RawDigit from an art-ROOT file with gallery and write a flat TTree
// that plain ROOT / python (uproot) can read without LArSoft.
//
//   root -l -b -q 'rawdigit_to_tree.C("X_rawdump.root", "rawdigits.root")'
//   [optional 3rd argument: input tag, default "tpcrawdecoder:daq"]
//
// Output tree "raw", one entry per (event, channel):
//   run, event, channel (offline channel number), pedestal, adc[nticks] (raw ADC, not pedestal-subtracted)
// Must run inside the container after dunesw setup (gallery + lardataobj dictionaries).

#include <iostream>
#include <string>
#include <vector>

#include "canvas/Utilities/InputTag.h"
#include "gallery/Event.h"
#include "gallery/Handle.h"
#include "lardataobj/RawData/RawDigit.h"
#include "lardataobj/RawData/raw.h"

#include "TFile.h"
#include "TTree.h"

void rawdigit_to_tree(std::string const& infile, std::string const& outfile = "rawdigits.root",
                      std::string const& tag = "tpcrawdecoder:daq")
{
  art::InputTag const rd_tag{tag};

  TFile fout(outfile.c_str(), "recreate");
  TTree tree("raw", "decoded raw waveforms, one entry per channel");
  int run = 0, event = 0, channel = 0;
  float pedestal = 0;
  std::vector<short> adc;
  tree.Branch("run", &run);
  tree.Branch("event", &event);
  tree.Branch("channel", &channel);
  tree.Branch("pedestal", &pedestal);
  tree.Branch("adc", &adc);

  for (gallery::Event ev({infile}); !ev.atEnd(); ev.next()) {
    run = ev.eventAuxiliary().run();
    event = ev.eventAuxiliary().event();
    auto const& digits = *ev.getValidHandle<std::vector<raw::RawDigit>>(rd_tag);
    std::cout << "run " << run << " event " << event << ": " << digits.size() << " RawDigits";
    if (!digits.empty()) std::cout << ", " << digits.front().Samples() << " ticks";
    std::cout << std::endl;

    for (auto const& rd : digits) {
      channel = rd.Channel();
      pedestal = rd.GetPedestal();
      adc.assign(rd.Samples(), 0);
      raw::Uncompress(rd.ADCs(), adc, rd.GetPedestal(), rd.Compression());  // no-op if not compressed
      tree.Fill();
    }
  }
  fout.cd();  // gallery changes the current ROOT directory when it opens the input
  tree.Write();
  std::cout << "wrote " << tree.GetEntries() << " channel waveforms to " << outfile << std::endl;
}
