// dump_wires.C -- flatten recob::Wire (one label) from an art file into a TTree
//   root -l -b -q 'dump_wires.C("in.root","wclsdatavd:gauss:pdvdnfsptrad","out.root")'
// Tree "wires": event, channel, view, signal (dense, one entry per wire).
#include "gallery/Event.h"
#include "canvas/Utilities/InputTag.h"
#include "lardataobj/RecoBase/Wire.h"
#include "TFile.h"
#include "TTree.h"
#include <vector>
#include <string>
#include <iostream>

void dump_wires(const char* in, const char* tag, const char* out)
{
  art::InputTag itag(tag);
  TFile fo(out, "RECREATE");
  TTree t("wires", std::string("recob::Wire ").append(tag).c_str());
  int event = 0, channel = 0, view = 0;
  std::vector<float> signal;
  t.Branch("event", &event);
  t.Branch("channel", &channel);
  t.Branch("view", &view);
  t.Branch("signal", &signal);
  for (gallery::Event ev({std::string(in)}); !ev.atEnd(); ev.next()) {
    event = ev.eventAuxiliary().event();
    auto const& wires = *ev.getValidHandle<std::vector<recob::Wire>>(itag);
    std::cout << "event " << event << ": " << wires.size() << " wires" << std::endl;
    for (auto const& w : wires) {
      channel = w.Channel();
      view = w.View();
      signal = w.Signal();
      t.Fill();
    }
  }
  fo.Write();
}
