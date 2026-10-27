#include <algorithm>
#include <chrono>
#include <functional>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "lardataobj/Utilities/sparse_vector.h"
#include "canvas/Utilities/InputTag.h"
#include "gallery/Event.h"
#include "gallery/Handle.h"

#include "lardataobj/RawData/RawDigit.h"

#include "TFile.h"
#include "TH1I.h"
#include "TH2I.h"
#include "TInterpreter.h"
#include "TROOT.h"

using namespace art;
using namespace std;
using namespace std::chrono;

void
vd_convert_artroot_raw(std::string const& filename)
{
  // PDVD per-channel-corrected RawDigit product, written by the calibration NF.
  //
  // The eleccalib file contains several "wclsdatavdfilter:raw" products under
  // different process names: the upstream uncorrected one (pdvdkeepupstage1)
  // and the freshly CORRECTED one from this calibration job (pdvdeleccalib for
  // the reco_stage1 path, pdvdeleccalibh5 for the from-HDF5 path).  We want the
  // corrected product, so try the calib process names first and fall back.
  // (A bare "wclsdatavdfilter:raw" is ambiguous across processes.)
  std::vector<InputTag> candidate_tags{
    InputTag{ "wclsdatavdfilter:raw:pdvdeleccalib" },
    InputTag{ "wclsdatavdfilter:raw:pdvdeleccalibh5" },
    InputTag{ "wclsdatavdfilter:raw:pdvdkeepupstage1" },
  };

  vector<string> filenames(1, filename);

  int n = 0;
  for (gallery::Event ev(filenames); !ev.atEnd(); ev.next()) {

    if (n > 0) break;  // keep original behavior: process only first event

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
      std::cout << "ERROR: no non-empty wclsdatavdfilter:raw product found." << std::endl;
      return;
    }
    std::cout << "reading RawDigits from tag: " << rawdigits_tag.encode() << std::endl;

    auto const& rawdigits = *rawdigits_handle;

    if (rawdigits.empty()) {
      std::cout << "WARNING: no RawDigit found." << std::endl;
      return;
    }

    // ------------------------------------------------------------
    // Determine channel range and nticks directly from the data
    // ------------------------------------------------------------
    int minChannel = std::numeric_limits<int>::max();
    int maxChannel = std::numeric_limits<int>::lowest();
    int maxSamples = 0;
    int minSamples = std::numeric_limits<int>::max();

    for (auto const& rd: rawdigits) {
      int ch = rd.Channel();
      int ns = rd.Samples();

      if (ch < minChannel) minChannel = ch;
      if (ch > maxChannel) maxChannel = ch;
      if (ns > maxSamples) maxSamples = ns;
      if (ns < minSamples) minSamples = ns;
    }

    if (minChannel > maxChannel || maxSamples <= 0) {
      std::cout << "ERROR: invalid channel/sample metadata." << std::endl;
      return;
    }

    int nchannels = maxChannel - minChannel + 1;
    int nticks = maxSamples;

    std::cout << "RawDigit summary:" << std::endl;
    std::cout << "  number of rawdigits = " << rawdigits.size() << std::endl;
    std::cout << "  channel range       = [" << minChannel << ", " << maxChannel << "]" << std::endl;
    std::cout << "  nchannels           = " << nchannels << std::endl;
    std::cout << "  min samples         = " << minSamples << std::endl;
    std::cout << "  max samples         = " << maxSamples << std::endl;
    std::cout << "  using nticks        = " << nticks << std::endl;

    if (minSamples != maxSamples) {
      std::cout << "WARNING: inconsistent RawDigit sample counts found."
                << " Histogram will use maxSamples = " << maxSamples << std::endl;
    }

    // ------------------------------------------------------------
    // Book histograms using the actual data range
    // x-axis labels are still the real offline channel numbers
    // ------------------------------------------------------------
    auto h_raw = new TH2I(
      "h_raw",
      "RawDigits;Channel;Tick",
      nchannels,
      minChannel - 0.5,
      maxChannel + 0.5,
      nticks,
      0,
      nticks
    );

    auto h_baseline = new TH1I(
      "h_baseline",
      "Pedestal;Channel;Pedestal",
      nchannels,
      minChannel - 0.5,
      maxChannel + 0.5
    );

    // ------------------------------------------------------------
    // Fill histograms
    // ------------------------------------------------------------
    int printed = 0;
    for (auto const& rd: rawdigits) {

      int channel = rd.Channel();
      int nSamples = rd.Samples();
      double pedestal = rd.GetPedestal();

      if (printed < 5) {
        std::cout << "  ch=" << channel
                  << " samples=" << nSamples
                  << " pedestal=" << pedestal
                  << std::endl;
        printed++;
      }

      // TH1/TH2 bin number for the actual channel value
      int chBin = h_raw->GetXaxis()->FindBin(channel);

      h_baseline->SetBinContent(chBin, pedestal);

      int nsToFill = std::min(nSamples, nticks);
      for (int j = 0; j < nsToFill; j++) {
        h_raw->SetBinContent(chBin, j + 1, rd.ADC(j) - pedestal);
      }
    }

    auto ofile = TFile::Open(Form("hraw-pdvd-magnify-%d-%d.root", aux.run(), aux.event()), "recreate");
    h_raw->Write();
    h_baseline->Write();
    ofile->Close();

    std::cout << "wrote output file: "
              << Form("hraw-pdvd-magnify-%d-%d.root", aux.run(), aux.event())
              << std::endl;

    n++;
  }
}
