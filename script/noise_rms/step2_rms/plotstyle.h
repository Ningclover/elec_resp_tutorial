#ifndef PLOTSTYLE_H
#define PLOTSTYLE_H
//
// Plot-style helpers for the noise plots. Loads rootlogon.C.c (fonts,
// margins) and adds small helpers for labels, legends and axes.
// Only changes how the plots look, not the numbers.
//
#include "TAxis.h"
#include "TPad.h"
#include "TH1.h"
#include "TLatex.h"
#include "TLegend.h"
#include "TROOT.h"
#include "TSystem.h"
#include "TStyle.h"

// Location of rootlogon.C.c is taken from the PLOT_STYLE_DIR environment
// variable (set by run_rms_pdvd.sh).
inline void plot_style()
{
    const char *env = gSystem->Getenv("PLOT_STYLE_DIR");
    TString dir = env && env[0] ? env : "../..";
    TString logon = dir + "/rootlogon.C.c";
    if (gSystem->AccessPathName(logon)) {
        std::cout << "[plot_style] WARNING: " << logon
                  << " not found, keeping current ROOT style" << std::endl;
        return;
    }
    // Load, then call rootlogon() by name: ROOT derives the auto-called
    // function from the file name (rootlogon_C for rootlogon.C.c), which
    // does not exist, so ".x" alone only warns and falls back to ".L".
    gROOT->ProcessLine(TString::Format(".L %s", logon.Data()));
    gROOT->ProcessLine("rootlogon();");
}

// rootlogon switches the pad title off (gStyle->SetOptTitle(0)), because a
// ROOT pad title sits outside the frame.
// Where a panel still needs to identify itself -- which detector, which
// plane -- draw a short label *inside* the frame instead.
// y defaults to 0.86, not 0.90: SetTextAlign(11) anchors the text by its
// *bottom* edge, so at the 0.058 text size a label at 0.90 has its ascenders
// running into the top frame line (now 3 units thick).
inline void plot_label(const char *text, double x = 0.20, double y = 0.86)
{
    TLatex *t = new TLatex();
    t->SetNDC();
    // Follow rootlogon's face rather than hardcoding one, so the in-frame
    // label stays in the same font as the axes.
    t->SetTextFont(gStyle->GetTextFont());
    t->SetTextSize(0.058);   // matches rootlogon's tick-label size
    t->SetTextAlign(11);     // left, bottom
    t->DrawLatex(x, y, text);
}

// Apply the pad margins to the current pad.
//
// These macros call gPad->SetRightMargin() etc. for their own (differently
// sized) figures, and a pad keeps whatever margins it was last given, so the
// gStyle margins from rootlogon do not survive.  Call this right before
// drawing a final figure -- the font sizes need this much room or
// the axis titles are clipped.
inline void plot_pad()
{
    if (!gPad) return;
    gPad->SetTopMargin(gStyle->GetPadTopMargin());
    gPad->SetRightMargin(gStyle->GetPadRightMargin());
    gPad->SetBottomMargin(gStyle->GetPadBottomMargin());
    gPad->SetLeftMargin(gStyle->GetPadLeftMargin());
    gPad->SetTicks(1, 1);
}

// Stamp the current gStyle's font/size/offset settings onto one histogram.
//
// gStyle only supplies defaults at *creation* time.  These macros build all
// their histograms up front (in Init()) and style them at draw time, so the
// axis attributes they carry are ROOT's defaults, not the plots's, and
// ForceStyle() alone does not reach them.  Call this on each histogram just
// before drawing it.
inline void plot_axes(TH1 *h)
{
    if (!h) return;
    TAxis *ax[3] = {h->GetXaxis(), h->GetYaxis(), h->GetZaxis()};
    const char *which[3] = {"x", "y", "z"};
    for (int i = 0; i < 3; ++i) {
        if (!ax[i]) continue;
        ax[i]->SetLabelFont(gStyle->GetLabelFont(which[i]));
        ax[i]->SetLabelSize(gStyle->GetLabelSize(which[i]));
        ax[i]->SetTitleFont(gStyle->GetTitleFont(which[i]));
        ax[i]->SetTitleSize(gStyle->GetTitleSize(which[i]));
        ax[i]->SetTickLength(gStyle->GetTickLength(which[i]));
    }
    // Centre the x/y titles along their axis.  gStyle's SetTitleCenterX/Y are
    // only consulted at histogram creation and TStyle exposes no getter for
    // them, so centre unconditionally here -- rootlogon asks for centred
    // titles, and every histogram drawn through plot_axes is a final figure.
    h->GetXaxis()->CenterTitle(true);
    h->GetYaxis()->CenterTitle(true);
    h->GetXaxis()->SetTitleOffset(gStyle->GetTitleOffset("x"));
    h->GetYaxis()->SetTitleOffset(gStyle->GetTitleOffset("y"));
    h->GetZaxis()->SetTitleOffset(gStyle->GetTitleOffset("z"));
    // gStyle's hist line width, like its font sizes, is only applied at
    // histogram *creation* time.  The RMS histograms are built in Init(),
    // before plot_style() runs, so they would keep ROOT's hairline default
    // while the FFT ones (built afterwards) come out at the width.
    // Stamp it here so every histogram drawn through plot_axes matches.
    h->SetLineWidth(gStyle->GetHistLineWidth());
    // Same story for the frame: gStyle's frame line width reaches a pad only
    // when the pad is created, and these macros reuse one long-lived canvas
    // built before plot_style().  Stamp it onto the current pad too.
    if (gPad) gPad->SetFrameLineWidth(gStyle->GetFrameLineWidth());
}

// Suppress the first bin of a filtered ("after noise filter") RMS histogram.
//
// Bin 1 of the ENC distributions spans 0-12 e-, and collects channels whose
// filtered RMS is essentially zero (dead/empty channels that survive the bad
// channel cut).  That is a readout artefact rather than a noise measurement,
// and it draws a tall spike at the left edge that distracts from the actual
// distribution.  Zero the content so the drawn line skips it; the axis range
// and the raw histogram are left untouched.
inline void drop_first_bin(TH1 *h)
{
    if (!h) return;
    h->SetBinContent(1, 0.0);
    h->SetBinError(1, 0.0);
}

// A legend with the font and no border/fill.
inline TLegend *plot_legend(double x1, double y1, double x2, double y2)
{
    TLegend *leg = new TLegend(x1, y1, x2, y2);
    leg->SetBorderSize(0);
    leg->SetFillStyle(0);
    leg->SetTextFont(gStyle->GetLegendFont());
    leg->SetTextSize(0.055);
    return leg;
}

#endif // PLOTSTYLE_H
