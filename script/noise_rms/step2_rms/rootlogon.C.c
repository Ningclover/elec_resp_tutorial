#include <iostream>
#include "TGaxis.h"

// ROOT plot style (fonts, margins, ticks) for the noise plots.
// Only changes how the plots look, not the numbers.
void rootlogon()
{
  std::cout << "[rootlogon] plot style loaded" << std::endl;

  // ---- plain black on white ------------------------------------------
  Int_t icol = 0; // WHITE
  gStyle->SetFrameBorderMode(icol);
  gStyle->SetFrameFillColor(icol);
  gStyle->SetCanvasBorderMode(icol);
  gStyle->SetCanvasColor(icol);
  gStyle->SetPadBorderMode(icol);
  gStyle->SetPadColor(icol);
  gStyle->SetStatColor(icol);

  gStyle->SetPaperSize(20, 26);

  // ---- margins -------------------------------------------------------
  // Large enough that the (large) axis titles below are never clipped.
  gStyle->SetPadTopMargin(0.06);
  gStyle->SetPadRightMargin(0.05);
  gStyle->SetPadBottomMargin(0.16);
  gStyle->SetPadLeftMargin(0.17);

  // ---- fonts ---------------------------------------------------------
  // Font 132 = Times; sizes are fractions of the pad height.
  Int_t    font  = 132;
  Double_t tsize = 0.065;   // axis titles
  Double_t lsize = 0.055;   // tick labels, a little smaller

  gStyle->SetTextFont(font);
  gStyle->SetTextSize(tsize);

  const char *axes[] = {"x", "y", "z"};
  for (int i = 0; i < 3; ++i) {
    gStyle->SetLabelFont(font, axes[i]);
    gStyle->SetTitleFont(font, axes[i]);
    gStyle->SetLabelSize(lsize, axes[i]);
    gStyle->SetTitleSize(tsize, axes[i]);
  }

  // Axis-title offsets: keep the (now larger) titles clear of the labels.
  gStyle->SetTitleXOffset(1.05);
  gStyle->SetTitleYOffset(1.25);
  gStyle->SetTitleOffset(1.0, "z");

  // Axis titles are centred along their axis (ROOT parks x at the right end
  // and y at the top by default; matplotlib centres both).  TStyle has no
  // setter for this, so plot_axes() calls CenterTitle() per histogram.

  // Legend: same font, no border/fill, so it stays unobtrusive.
  gStyle->SetLegendFont(font);
  gStyle->SetLegendTextSize(lsize);
  gStyle->SetLegendBorderSize(0);
  gStyle->SetLegendFillColor(0);

  // ---- lines and markers ---------------------------------------------
  gStyle->SetMarkerStyle(20);
  gStyle->SetMarkerSize(0.9);
  gStyle->SetHistLineWidth(2.);
  gStyle->SetFrameLineWidth(3);
  gStyle->SetLineStyleString(2, "[12 12]"); // postscript dashes

  // no x error bars, no error-bar caps (figure-guideline convention)
  gStyle->SetErrorX(0.0001);
  gStyle->SetEndErrorSize(0.);

  // ---- decorations ---------------------------------------------------
  // No pad title: the caption carries the description instead.
  gStyle->SetOptTitle(0);
  gStyle->SetOptStat(0);
  gStyle->SetOptFit(0);

  // Centre the axis titles (ROOT's default parks them at the frame edge,
  // which reads oddly next to the centred axis labels).
  gStyle->SetTitleAlign(22);
  // Keep the axis in plain numbers: a "x10^3" exponent block would be drawn
  // just outside the frame and gets clipped at these font sizes.
  TGaxis::SetMaxDigits(5);

  // ticks on all four sides, and a sane number of divisions
  gStyle->SetPadTickX(1);
  gStyle->SetPadTickY(1);
  // Ticks inherit the frame line width, so they thicken with it; give them a
  // little extra length as well so they stay legible against the heavier
  // frame (ROOT's default is 0.03).
  gStyle->SetTickLength(0.04, "x");
  gStyle->SetTickLength(0.04, "y");
  gStyle->SetNdivisions(505, "x");
  gStyle->SetNdivisions(505, "y");

  gROOT->ForceStyle();
}
