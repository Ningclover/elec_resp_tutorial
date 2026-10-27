// vd_convert_orig_raw.C
//
// Dump BOTH the uncorrected (orig = tpcrawdecoder:daq) and the per-channel
// CORRECTED (raw = wclsdatavdfilter:raw) RawDigits from a reprocessed PDVD
// eleccalib art-ROOT into ONE magnify file holding two TH2I:
//   h_daq   -- orig  (Without Deconvolution)
//   h_raw   -- raw   (With  Deconvolution)
//
// This replaces the need for Karla's separate hdaq-/hraw- magnify files: a
// single self-reprocessed file carries both sides of the comparison.
//
// Output: ourmag-pdvd-<run>-<event>.root   (both TH2I, pedestal-subtracted)
//
// Usage (inside container, dunesw sourced):
//   root -b -q 'vd_convert_orig_raw.C("<...eleccalib(.|_h5.)root>")'

#include <algorithm>
#include <iostream>
#include <limits>
#include <map>
#include <string>
#include <vector>

#include "lardataobj/Utilities/sparse_vector.h"
#include "canvas/Utilities/InputTag.h"
#include "gallery/Event.h"
#include "gallery/Handle.h"
#include "lardataobj/RawData/RawDigit.h"

#include "TFile.h"
#include "TH2I.h"

using namespace art;
using namespace std;

static const vector<raw::RawDigit>*
pick(gallery::Event& ev, const vector<InputTag>& tags,
     gallery::Handle<vector<raw::RawDigit>>& keep, string& used) {
  for (auto const& t : tags) {
    gallery::Handle<vector<raw::RawDigit>> h;
    if (ev.getByLabel(t, h) && h.isValid() && !h->empty()) {
      keep = h; used = t.encode(); return &(*h);
    }
  }
  return nullptr;
}

static TH2I* book_fill(const vector<raw::RawDigit>& digs, const char* name) {
  int minCh = numeric_limits<int>::max(), maxCh = numeric_limits<int>::lowest(), maxNs = 0;
  for (auto const& rd : digs) {
    int ch = (int)rd.Channel(), ns = (int)rd.Samples();
    minCh = min(minCh, ch); maxCh = max(maxCh, ch); maxNs = max(maxNs, ns);
  }
  if (minCh > maxCh || maxNs <= 0) return nullptr;
  int nch = maxCh - minCh + 1;
  auto* h = new TH2I(name, "RawDigits;Channel;Tick",
                     nch, minCh - 0.5, maxCh + 0.5, maxNs, 0, maxNs);
  for (auto const& rd : digs) {
    int chBin = h->GetXaxis()->FindBin(rd.Channel());
    double ped = rd.GetPedestal();
    int ns = min((int)rd.Samples(), maxNs);
    for (int j = 0; j < ns; ++j) h->SetBinContent(chBin, j + 1, rd.ADC(j) - ped);
  }
  return h;
}

void vd_convert_orig_raw(std::string const& filename) {
  vector<InputTag> orig_tags{
    InputTag{"tpcrawdecoder:daq"},
    InputTag{"tpcrawdecoder:daq:pdvdeleccalibh5"},
    InputTag{"tpcrawdecoder:daq:pdvdeleccalib"},
  };
  vector<InputTag> raw_tags{
    InputTag{"wclsdatavdfilter:raw:pdvdeleccalibh5"},
    InputTag{"wclsdatavdfilter:raw:pdvdeleccalib"},
    InputTag{"wclsdatavdfilter:raw:pdvdkeepupstage1"},
  };

  vector<string> files(1, filename);
  for (gallery::Event ev(files); !ev.atEnd(); ev.next()) {
    auto aux = ev.eventAuxiliary();
    cout << "processing event: " << aux.run() << "-" << aux.event() << endl;

    gallery::Handle<vector<raw::RawDigit>> ho, hr;
    string otag, rtag;
    auto* orig = pick(ev, orig_tags, ho, otag);
    auto* rawc = pick(ev, raw_tags,  hr, rtag);
    if (!orig) { cout << "ERROR: no orig (tpcrawdecoder:daq) digits\n"; return; }
    if (!rawc) { cout << "ERROR: no raw  (wclsdatavdfilter:raw) digits\n"; return; }
    cout << "  orig tag: " << otag << "  (" << orig->size() << ")\n";
    cout << "  raw  tag: " << rtag << "  (" << rawc->size() << ")\n";

    TH2I* h_daq = book_fill(*orig, "h_daq");
    TH2I* h_raw = book_fill(*rawc, "h_raw");
    if (!h_daq || !h_raw) { cout << "ERROR: failed to book histos\n"; return; }

    auto* ofile = TFile::Open(Form("ourmag-pdvd-%d-%d.root", aux.run(), aux.event()), "recreate");
    h_daq->Write();
    h_raw->Write();
    ofile->Close();
    cout << "wrote ourmag-pdvd-" << aux.run() << "-" << aux.event() << ".root (h_daq + h_raw)\n";
    return;  // first event only
  }
}
