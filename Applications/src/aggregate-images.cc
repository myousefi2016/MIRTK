/*
 * Medical Image Registration ToolKit (MIRTK)
 *
 * Copyright 2017 Imperial College London
 * Copyright 2017 Andreas Schuh
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "mirtk/Common.h"
#include "mirtk/Options.h"

#include "mirtk/IOConfig.h"

#include "mirtk/GenericImage.h"
#include "mirtk/DataStatistics.h"
#include "mirtk/Histogram1D.h"

#include <functional>

using namespace mirtk;
using namespace mirtk::data::statistic;


// =============================================================================
// Help
// =============================================================================

/// Print program usage information
void PrintHelp(const char* name)
{
  cout << "\n";
  cout << "Usage: " << name << " <mode> <image> <image>... -output <file> [options]\n";
  cout << "\n";
  cout << "Description:\n";
  cout << "  Aggregates multiple (co-registered) input images into a single output image\n";
  cout << "  or numbers such as a statistic evaluated for a common region of interest\n";
  cout << "  given the intensity samples within this region in all input images.\n";
  cout << "  The input images have to be defined in the same discrete finite image space.\n";
  cout << "\n";
  cout << "Required arguments:\n";
  cout << "  <mode>\n";
  cout << "      Name of function used to aggregate input values:\n";
  cout << "      - ``mu``, ``mean``, ``average``, ``avg``: Mean intensity.\n";
  cout << "      - ``sd``, ``stdev``, ``stddev``, ``sigma``: Standard deviation.\n";
  cout << "      - ``gini``, ``gini-coefficient``: Gini coefficient in [0, 1].\n";
  cout << "      - ``theil``, ``theil-index``: Theil index, equivalent to GE(1).\n";
  cout << "      - ``entropy-index``, ``ge``: Generalized entropy index (GE), see also :option:`-alpha`.\"\n";
  cout << "      - ``entropy``: Shannon entropy, see also :option:`-bins` and :option:`-parzen`.\n";
  cout << "  <image>\n";
  cout << "      File names of at least two input intensity images.\n";
  cout << "  -output <file>\n";
  cout << "      Voxel-wise aggregate image.\n";
  cout << "\n";
  cout << "Optional arguments:\n";
  cout << "  -padding <value>\n";
  cout << "      Background value in input images of voxels to be ignored during\n";
  cout << "      intensity normalization. (default: NaN)\n";
  cout << "  -normalize, -normalization <mode>\n";
  cout << "      Input intensity normalization:\n";
  cout << "      - ``none``:    Use input intensities unmodified. (default)\n";
  cout << "      - ``mean``:    Divide by mean foreground value.\n";
  cout << "      - ``median``:  Divide by median foreground value.\n";
  cout << "      - ``z-score``: Subtract mean and divide by standard deviation.\n";
  cout << "      - ``unit``:    Rescale input intensities to [0, 1].\n";
  cout << "  -alpha <value>\n";
  cout << "      Alpha value of the generalized entropy index, where alpha=0 is the mean log deviation, alpha=1\n";
  cout << "      is the Theil coefficient, and alpha=2 is half the squared coefficient of variation. (default: 0)\n";
  cout << "  -bins\n";
  cout << "      No. of bins used for histogram-based aggregation functions. (default: 64)\n";
  cout << "  -parzen [yes|no|on|off]\n";
  cout << "      Use Parzen window based histogram estimation. (default: off)\n";
  cout << "  -intersection [yes|no|on|off]\n";
  cout << "      Calculate aggregation function for every voxel for which no input value is\n";
  cout << "      equal the specified :option:`-padding` value. By default, only voxels for which\n";
  cout << "      all input values are equal to the :option:`-padding` value are excluded. (default: off)\n";
  PrintCommonOptions(cout);
  cout << endl;
}

// =============================================================================
// Types
// =============================================================================

// Type of input images
typedef RealPixel                InputType;
typedef GenericImage<InputType>  InputImage;
typedef Array<InputType>         InputArray;

// Type of output image
typedef float                     OutputType;
typedef GenericImage<OutputType>  OutputImage;

// Type of intensity image normalization
enum NormalizationMode
{
  Normalization_None,       ///< Use input intensity values unmodified
  Normalization_Mean,       ///< Divide input image values by mean intensity
  Normalization_Median,     ///< Divide input image values by median intensity
  Normalization_ZScore,     ///< Subtract mean intensity and divide by standard deviation
  Normalization_UnitRange   ///< Rescale input intensities to [0, 1]
};

// Enumeration of implemented aggregation functions
enum AggregationMode
{
  AM_Mean,          ///< Mean value
  AM_Median,        ///< Median value
  AM_StDev,         ///< Standard deviation
  AM_Gini,          ///< Gini coefficient
  AM_Theil,         ///< Theil coefficient, i.e., GE(1)
  AM_EntropyIndex,  ///< Generalized entropy index (GE)
  AM_Entropy        ///< Shannon entropy
};

// Type of functions used to aggregate set of values
typedef std::function<OutputType(InputArray &)> AggregationFunction;

// =============================================================================
// Auxiliary functions
// =============================================================================

// -----------------------------------------------------------------------------
/// Convert string to aggregation mode enumeration value
bool FromString(const char *str, AggregationMode &value)
{
  const string lstr = ToLower(str);
  if (lstr == "mean") {
    value = AM_Mean;
  } else if (lstr == "median") {
    value = AM_Median;
  } else if (lstr == "stddev" || lstr == "stdev" || lstr == "sdev" || lstr == "sd" || lstr == "sigma") {
    value = AM_StDev;
  } else if (lstr == "gini" || lstr == "gini-coefficient") {
    value = AM_Gini;
  } else if (lstr == "theil" || lstr == "theil-index") {
    value = AM_Theil;
  } else if (lstr == "entropy-index" || lstr == "ge" || lstr == "generalized-entropy-index") {
    value = AM_EntropyIndex;
  } else if (lstr == "entropy" || lstr == "shannon-entropy") {
    value = AM_Entropy;
  } else {
    return false;
  }
  return true;
}

// -----------------------------------------------------------------------------
/// Get foreground mask for use with DataStatistic functions
UniquePtr<bool[]> ForegroundMaskArray(const InputImage &image)
{
  const int n = image.NumberOfVoxels();
  UniquePtr<bool[]> mask(new bool[n]);
  for (int i = 0; i < n; ++i) {
    mask[i] = (!IsNaN(image.GetAsDouble(i)) && image.IsForeground(i));
  }
  return mask;
}

// -----------------------------------------------------------------------------
/// Normalize image intensities
void Normalize(InputImage &image, NormalizationMode mode = Normalization_ZScore)
{
  if (mode == Normalization_None) return;

  const auto mask   = ForegroundMaskArray(image);
  const int  n      = image.NumberOfVoxels();
  auto * const data = image.Data();

  double s = 1.;
  double t = 0.;

  switch (mode) {
    case Normalization_Mean: {
      double mean = Mean::Calculate(n, data, mask.get());
      if (!fequal(mean, 0.)) s = 1. / mean;
    } break;

    case Normalization_Median: {
      double median = Median::Calculate(n, data, mask.get());
      if (!fequal(median, 0.)) s = 1. / median;
    } break;

    case Normalization_ZScore: {
      double mean, sigma;
      NormalDistribution::Calculate(mean, sigma, n, data, mask.get());
      if (fequal(sigma, 0.)) {
        s = 1. / sigma;
        t = - mean / sigma;
      } else {
        t = - mean;
      }
    } break;

    case Normalization_UnitRange: {
      double min_value, max_value;
      Extrema::Calculate(min_value, max_value, n, data, mask.get());
      double range = max_value - min_value;
      if (!fequal(range, 0.)) {
        s = 1. / range;
        t = - min_value / range;
      } else {
        t = - min_value;
      }
    } break;

    default: break;
  };

  if (s != 1. || t != 0.) {
    auto p = data;
    auto m = mask.get();
    for (int i = 0; i < n; ++i, ++p, ++m) {
      if (*m) (*p) = s * (*p) + t;
    }
  }
}

// -----------------------------------------------------------------------------
/// Aggregate values at each voxel
struct AggregateValuesAtEachVoxel
{
  Array<InputImage>  *_Images;
  OutputImage        *_Output;
  AggregationFunction _Function;

  void operator ()(const blocked_range<int> &voxels) const
  {
    InputArray values(_Images->size());
    const int n = static_cast<int>(_Images->size());
    for (int vox = voxels.begin(); vox < voxels.end(); ++vox) {
      if (_Output->IsForeground(vox)) {
        for (int i = 0; i < n; ++i) {
          values[i] = (*_Images)[i].Get(vox);
        }
        _Output->Put(vox, _Function(values));
      }
    }
  }
};

// =============================================================================
// Measures of dispersion
// =============================================================================

// -----------------------------------------------------------------------------
/// Evaluate Gini coefficient of data samples
///
/// \param[in,out] samples Sampled values of distribution.
/// \note Modifies the input sample values to be non-positive and sorts them ascending.
///
/// \returns Gini coefficient in [0, 1], where the Gini coefficient is 0 when all sample
///          values are equal and close to 1 when a single value differs.
///
/// \see http://neuroplausible.com/gini
/// \see http://www.ellipsix.net/blog/2012/11/the-gini-coefficient-for-distribution-inequality.html
template <class T>
double GiniCoefficient(Array<T> &samples)
{
  double sum1 = 0., sum2 = 0.;
  const int n = static_cast<int>(samples.size());
  // Shift values to be all positive
  T shift = MinElement(samples);
  if (shift <= static_cast<T>(0)) {
    if (numeric_limits<T>::is_integer) shift -= static_cast<T>(1);
    else                               shift -= static_cast<T>(1e-6);
    Transform(samples, bind2nd(minus<T>(), shift));
  }
  Sort(samples);
  for (int i = 0; i < n; ++i) {
    // Note: rank i is zero-based, hence 2 * (i + 1) - n - 1 = 2 * i + 2 - n - 1
    sum1 += static_cast<double>(2 * i - n + 1) * static_cast<double>(samples[i]);
    sum2 += samples[i];
  }
  return sum1 / (n * sum2);
}

// -----------------------------------------------------------------------------
/// Evaluate general entropy index
///
/// \param[in] samples Sampled values of distribution.
/// \param[in] alpha   Weight given to distances between values at different
///                    parts of the distribution. For alpha = 0, the entropy
///                    index is equal the mean log deviation. For alpha = 1,
///                    it is equal the Theil index. For alpha = 2, it is
///                    half the squared coefficient of variation (i.e, the
///                    standard deviation divided by the mean value).
///
/// \note Modifies the input sample values to be non-positive.
///
/// \see https://en.wikipedia.org/wiki/Generalized_entropy_index
/// \see https://en.wikipedia.org/wiki/Theil_index
template <class T>
double EntropyIndex(Array<T> &samples, int alpha = 1)
{
  if (alpha < 0) {
    Throw(ERR_InvalidArgument, __FUNCTION__, "alpha must be non-negative");
  }
  const int n = static_cast<int>(samples.size());
  if (n == 0) return 0.;
  // Shift values to be all positive
  T shift = MinElement(samples);
  if (shift <= static_cast<T>(0)) {
    if (numeric_limits<T>::is_integer) shift -= static_cast<T>(1);
    else                               shift -= static_cast<T>(1e-6);
    Transform(samples, bind2nd(minus<T>(), shift));
  }
  // Compute mean value
  double mean = 0.;
  for (int i = 0; i < n; ++i) {
    mean += samples[i];
  }
  mean /= n;
  // Evaluate sum of generalized entropy index
  double sum = 0., p;
  switch (alpha) {
    // Mean log deviation
    case 0: {
      for (int i = 0; i < n; ++i) {
        p = samples[i] / mean;
        sum -= log(p);
      }
    } break;
    // Theil index
    case 1: {
      for (int i = 0; i < n; ++i) {
        p = samples[i] / mean;
        sum += p * log(p);
      }
    } break;
    // Coefficient of variation (squared, half)
    case 2: {
      for (int i = 0; i < n; ++i) {
        sum += samples[i] * samples[i];
      }
      sum /= mean * mean;
      sum -= n;
      sum /= 2;
    } break;
    // Generalized entropy index
    default: {
      for (int i = 0; i < n; ++i) {
        p = samples[i] / mean;
        sum += pow(p, alpha);
      }
      sum -= n;
      sum /= alpha * (alpha - 1);
    } break;
  }
  return sum / n;
}

// =============================================================================
// Main
// =============================================================================

// -----------------------------------------------------------------------------
int main(int argc, char **argv)
{
  // Parse command arguments
  REQUIRES_POSARGS(3);

  AggregationMode mode;
  if (!FromString(POSARG(1), mode)) {
    FatalError("Invalid aggregation mode: " << POSARG(1));
  }

  const char        *output_name   = nullptr;
  NormalizationMode  normalization = Normalization_None;
  int                alpha         = 0;
  int                bins          = 64;
  bool               parzen        = false;
  double             padding       = NaN;
  bool               intersection  = false;

  for (ALL_OPTIONS) {
    if (OPTION("-output")) output_name = ARGUMENT;
    else if (OPTION("-normalization") || OPTION("-normalize")) {
      if (HAS_ARGUMENT) {
        const string arg = ToLower(ARGUMENT);
        bool bval;
        if (FromString(arg, bval)) {
          normalization = (bval ? Normalization_ZScore : Normalization_None);
        } else if (arg == "none") {
          normalization = Normalization_None;
        } else if (arg == "mean") {
          normalization = Normalization_Mean;
        } else if (arg == "median") {
          normalization = Normalization_Median;
        } else if (arg == "zscore" || arg == "z-score") {
          normalization = Normalization_ZScore;
        } else if (arg == "unit") {
          normalization = Normalization_UnitRange;
        } else {
          FatalError("Invalid -normalization mode: " << arg);
        }
      } else {
        normalization = Normalization_ZScore;
      }
    }
    else if (OPTION("-padding")) PARSE_ARGUMENT(padding);
    else if (OPTION("-alpha")) PARSE_ARGUMENT(alpha);
    else if (OPTION("-bins")) PARSE_ARGUMENT(bins);
    else HANDLE_BOOL_OPTION(parzen);
    else HANDLE_BOOL_OPTION(intersection);
    else HANDLE_COMMON_OR_UNKNOWN_OPTION();
  }
  if (!output_name) {
    FatalError("Option -output is required!");
  }

  // Initialize I/O factories
  InitializeIOLibrary();

  // Read input images
  Array<InputImage> images(NUM_POSARGS - 1);
  if (verbose) {
    cout << "Reading " << images.size() << " images...";
    cout.flush();
  }
  images[0].Read(POSARG(2));
  const int nvox = images[0].NumberOfVoxels();
  for (int i = 3; i <= NUM_POSARGS; ++i) {
    images[i - 2].Read(POSARG(i));
    if (images[i - 2].Attributes() != images[0].Attributes()) {
      if (verbose) cout << " failed" << endl;
      FatalError("Input image " << POSARG(i) << " has different attributes than previous input images!");
    }
  }
  if (verbose) cout << " done" << endl;

  // Replace background values by NaN to be able to identify background after normalization
  for (auto &image : images) {
    if (!IsNaN(padding)) {
      for (int vox = 0; vox < nvox; ++vox) {
        if (image(vox) == padding) {
          image(vox) = NaN;
        }
      }
    }
    image.PutBackgroundValueAsDouble(NaN);
  }

  // Normalize images
  if (normalization != Normalization_None) {
    if (verbose) {
      cout << "Normalizing images...";
      cout.flush();
    }
    for (auto &image : images) {
      Normalize(image, normalization);
    }
    if (verbose) cout << " done" << endl;
  }

  // Ensure all (normalized) intensities are positive
  if (mode == AM_Gini || mode == AM_Theil || mode == AM_EntropyIndex) {
    double min_value = inf;
    for (const auto &image : images) {
      double vmin, vmax;
      image.GetMinMaxAsDouble(vmin, vmax);
      if (vmin < min_value) min_value = vmin;
    }
    if (IsInf(min_value)) {
      FatalError("Neither input image seems to have any foreground given -padding value of " << padding);
    }
    min_value -= 1.;
    for (auto &image : images) {
      for (int vox = 0; vox < nvox; ++vox) {
        if (image.IsForeground(vox)) {
          image(vox) -= min_value;
        } else {
          image(vox) = 0.;
        }
      }
      image.PutBackgroundValueAsDouble(0.);
    }
  }

  // Initialize output image
  const double bg = (mode == AM_Mean ? NaN : 1e-3);
  OutputImage output(images[0].Attributes());
  if (!IsNaN(padding)) {
    if (intersection) {
      output = 0.;
      for (int vox = 0; vox < nvox; ++vox) {
        for (size_t i = 0; i < images.size(); ++i) {
          if (images[i].IsBackground(vox)) {
            output(vox) = bg;
            break;
          }
        }
      }
    } else {
      output = bg;
      for (int vox = 0; vox < nvox; ++vox) {
        for (size_t i = 0; i < images.size(); ++i) {
          if (images[i].IsForeground(vox)) {
            output(vox) = 0.;
            break;
          }
        }
      }
    }
  }
  output.PutBackgroundValueAsDouble(bg);
  if (verbose > 1) {
    int nbg = 0;
    for (int vox = 0; vox < nvox; ++vox) {
      if (output.IsBackground(vox)) ++nbg;
    }
    cout << "No. of foreground voxels = " << nvox - nbg << endl;
    cout << "No. of background voxels = " << nbg << endl;
  }

  // Evaluate aggregation function for samples given at each voxel
  if (verbose) {
    cout << "Performing voxel-wise aggregation...";
    cout.flush();
  }
  AggregateValuesAtEachVoxel eval;
  eval._Images = &images;
  eval._Output = &output;
  switch (mode) {
    case AM_Mean: {
      eval._Function = [](const InputArray &values) -> OutputType {
        const auto n = static_cast<int>(values.size());
        return static_cast<OutputType>(Mean::Calculate(n, values.data()));
      };
    } break;

    case AM_Median: {
      eval._Function = [](const InputArray &values) -> OutputType {
        const auto n = static_cast<int>(values.size());
        return static_cast<OutputType>(Median::Calculate(n, values.data()));
      };
    } break;

    case AM_StDev: {
      eval._Function = [](const InputArray &values) -> OutputType {
        const auto n = static_cast<int>(values.size());
        return static_cast<OutputType>(StDev::Calculate(n, values.data()));
      };
    } break;

    case AM_Gini: {
      eval._Function = [](InputArray &values) -> OutputType {
        return static_cast<OutputType>(GiniCoefficient(values));
      };
    } break;

    case AM_Theil: {
      eval._Function = [](InputArray &values) -> OutputType {
        return static_cast<OutputType>(EntropyIndex(values, 1));
      };
    } break;

    case AM_EntropyIndex: {
      eval._Function = [alpha](InputArray &values) -> OutputType {
        return static_cast<OutputType>(EntropyIndex(values, alpha));
      };
    } break;

    case AM_Entropy: {
      eval._Function = [bins, parzen](const InputArray &values) -> OutputType {
        const auto minmax = MinMaxElement(values);
        if (minmax.first >= minmax.second) {
          return 0.;
        }
        Histogram1D<int> hist(bins);
        hist.Min(static_cast<double>(minmax.first));
        hist.Max(static_cast<double>(minmax.second));
        for (auto value : values) {
          hist.AddSample(static_cast<double>(value));
        }
        if (parzen) hist.Smooth();
        return static_cast<OutputType>(hist.Entropy());
      };
    } break;

    default:
      if (verbose) cout << " failed" << endl;
      FatalError("Invalid aggregation mode: " << mode);
  };
  parallel_for(blocked_range<int>(0, nvox), eval);
  if (verbose) cout << " done" << endl;

  // Replace NaN's by minimum value
  OutputType omin, omax;
  output.GetMinMax(omin, omax);
  omin -= 1e-3;
  for (int vox = 0; vox < nvox; ++vox) {
    if (IsNaN(output(vox))) output(vox) = omin;
  }

  // Write output image
  if (verbose) {
    cout << "Writing result to " << output_name << "...";
    cout.flush();
  }
  output.Write(output_name);
  if (verbose) cout << " done" << endl;

  return 0;
}