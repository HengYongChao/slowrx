#include "dsp.h"
#include <cmath>

DSP::DSP() : m_fshift(0), m_sync_window(WINDOW_HANN511), m_decim_ratio(4),
  m_freq_if(1900), m_window(16) {

  m_window[WINDOW_HANN95]   = window::Hann(95);
  m_window[WINDOW_HANN127]  = window::Hann(127);
  m_window[WINDOW_HANN255]  = window::Hann(255);
  m_window[WINDOW_HANN511]  = window::Hann(511);
  m_window[WINDOW_HANN1023] = window::Hann(1023);
  m_window[WINDOW_HANN2047] = window::Hann(2047);
  m_window[WINDOW_CHEB47]   = {
    0.0004272315,0.0013212953,0.0032312239,0.0067664313,0.0127521667,0.0222058684,
    0.0363037629,0.0563165400,0.0835138389,0.1190416120,0.1637810511,0.2182020094,
    0.2822270091,0.3551233730,0.4354402894,0.5210045495,0.6089834347,0.6960162864,
    0.7784084484,0.8523735326,0.9143033652,0.9610404797,0.9901263448,1.0000000000,
    0.9901263448,0.9610404797,0.9143033652,0.8523735326,0.7784084484,0.6960162864,
    0.6089834347,0.5210045495,0.4354402894,0.3551233730,0.2822270091,0.2182020094,
    0.1637810511,0.1190416120,0.0835138389,0.0563165400,0.0363037629,0.0222058684,
    0.0127521667,0.0067664313,0.0032312239,0.0013212953,0.0004272315
  };

  m_fft_inbuf = fftw_alloc_complex(FFT_LEN);
  if (m_fft_inbuf == NULL) {
    perror("unable to allocate memory for FFT");
    exit(EXIT_FAILURE);
  }
  m_fft_outbuf = fftw_alloc_complex(FFT_LEN);
  if (m_fft_outbuf == NULL) {
    perror("unable to allocate memory for FFT");
    fftw_free(m_fft_inbuf);
    exit(EXIT_FAILURE);
  }
  for (int i=0; i<FFT_LEN; i++) {
    m_fft_inbuf[i][0]  = m_fft_inbuf[i][1]  = 0.0;
    m_fft_outbuf[i][0] = m_fft_outbuf[i][1] = 0.0;
  }

  m_mag = std::vector<double>(FFT_LEN);

  //m_fft_plan_small = fftw_plan_dft_1d(FFT_LEN_SMALL, m_fft_inbuf, m_fft_outbuf, FFTW_FORWARD, FFTW_ESTIMATE);
  m_fft_plan = fftw_plan_dft_1d(FFT_LEN, m_fft_inbuf, m_fft_outbuf, FFTW_FORWARD, FFTW_ESTIMATE);

  m_input = std::make_shared<Input>();

}

DSP::~DSP() {
  fftw_free(m_fft_inbuf);
  fftw_free(m_fft_outbuf);
}

std::shared_ptr<Input> DSP::getInput() {
  return m_input;
}

// the current moment, windowed
// will be written over buf
// which MUST fit FFT_LEN_BIG * fftw_complex
void DSP::calcWindowedFFT (WindowType win_type, int fft_len) {

  assert(fft_len >= m_window[win_type].size() / m_decim_ratio);

  for (int i=0; i<fft_len; i++)
    m_fft_inbuf[i][0] = m_fft_inbuf[i][1] = 0;

  double if_phi = 0;
  for (int i = 0; i < MOMENT_LEN; i++) {

    int win_i = i - MOMENT_LEN/2 + m_window[win_type].size()/2 ;

    if (win_i >= 0 && win_i < (int)m_window[win_type].size()) {
      double a;
      fftw_complex mixed;
      a = m_input->m_cirbuf.at(i);

      // mix to IF
      mixed[0] = a * cos(if_phi) - a * sin(if_phi);
      mixed[1] = a * cos(if_phi) + a * sin(if_phi);
      if_phi += 2 * M_PI * (-m_freq_if) / m_input->getSamplerate();

      // LPF
      // TODO

      // decimate
      if (win_i % m_decim_ratio == 0) {
        m_fft_inbuf[win_i / m_decim_ratio][0] = mixed[0] * m_window[win_type][win_i];
        m_fft_inbuf[win_i / m_decim_ratio][1] = mixed[1] * m_window[win_type][win_i];
      }
    }
  }

  fftw_execute(m_fft_plan);

}

double DSP::calcPeakFreq (double minf, double maxf, WindowType wintype) {

  //printf("   calcpeakfreq\n");
  int fft_len = FFT_LEN;

  calcWindowedFFT(wintype, fft_len);

  int peak_i = -1,peak_bin=0;
  int lobin = freq2bin(minf, fft_len);
  int hibin = freq2bin(maxf, fft_len);

  for (int bin = lobin-1; bin <= hibin+1; bin++) {
    int i = (bin >= 0 ? bin : fft_len + bin);

    m_mag.at(i) = complexMag(m_fft_outbuf[i]);
    if ( (bin >= lobin && bin <= hibin) &&
         (peak_i == -1 || m_mag.at(i) > m_mag.at(peak_i)) ) {
      peak_i = i;
      peak_bin = bin;
    }
    //printf("%6.2f ",m_mag.at(i));
  }
  //printf ("%d(%d) ",peak_i,peak_bin);

  double result = gaussianPeak(m_mag, peak_bin, true);

  //printf(" %.3f ",result);

  // In Hertz
  result = result / (fft_len*m_decim_ratio) * m_input->getSamplerate() + m_freq_if + m_fshift;


  // cheb47 @ 44100 can't resolve <1700 Hz
  //if (result < 1700 && wintype == WINDOW_CHEB47)
  //  result = calcPeakFreq (minf, maxf, WINDOW_HANN95);

  //printf("   ret\n");
  //printf("%.1f Hz\n",result);
  return result;

}

Wave DSP::calcBandPowerPerHz(const std::vector<std::vector<double>>& bands, WindowType wintype) {

  int fft_len = FFT_LEN;

  calcWindowedFFT(wintype, fft_len);

  Wave result;
  for (Wave band : bands) {
    double P = 0.0;
    double binwidth = 1.0 * m_input->getSamplerate() / fft_len;
    int nbins = 0;
    int lobin = freq2bin(band[0]+m_fshift, fft_len);
    int hibin = freq2bin(band[1]+m_fshift, fft_len);
    for (int bin = lobin; bin<=hibin; bin++) {
      int i = (bin >= 0 ? bin : fft_len + bin);
      P += pow(complexMag(m_fft_outbuf[i]), 2);
      nbins++;
    }
    P = (binwidth*nbins == 0 ? 0.0 : P/(binwidth*nbins));
    result.push_back(P);
  }
  return result;
}

WindowType DSP::bestWindowFor(SSTVMode Mode, double SNR) {
  WindowType WinType;

  //double samplesInPixel = 1.0 * samplerate_ * ModeSpec[Mode].tScan / ModeSpec[Mode].ScanPixels;

  if      (SNR >=  23.0 && Mode != MODE_PD180 && Mode != MODE_SDX)  WinType = WINDOW_CHEB47;
  else if (SNR >=  12.0)  WinType = WINDOW_HANN95;
  else if (SNR >=   8.0)  WinType = WINDOW_HANN127;
  else if (SNR >=   5.0)  WinType = WINDOW_HANN255;
  else if (SNR >=   4.0)  WinType = WINDOW_HANN511;
  else                    WinType = WINDOW_HANN1023;

  return WinType;
}

double DSP::calcVideoSNR () {
  const double t = m_input->get_t();

  if (t >= m_next_snr_time) {
    std::vector<double> bands = calcBandPowerPerHz(
        {{FREQ_SYNC-1000,FREQ_SYNC-200},
        {FREQ_BLACK,FREQ_WHITE},
        {FREQ_WHITE+400, FREQ_WHITE+700}}
    );
    double Pvideo_plus_noise = bands[1];
    double Pnoise_only       = (bands[0] + bands[2]) / 2;
    double Psignal = Pvideo_plus_noise - Pnoise_only;

    m_SNR = ((Pnoise_only == 0 || Psignal / Pnoise_only < .01) ?
        -20.0 : 10 * log10(Psignal / Pnoise_only));

    m_next_snr_time = t + 50e-3;
  }

  return m_SNR;
}

double DSP::calcSyncPower () {
  std::vector<double> bands = calcBandPowerPerHz(
      {{FREQ_SYNC-50,FREQ_SYNC+50},
      {FREQ_BLACK,FREQ_WHITE}},
      m_sync_window
  );
  double sync;
  if (bands[1] == 0.0 || bands[0] > 4 * bands[1]) {
    sync = 2.0;
  } else {
    sync = bands[0] / (2 * bands[1]);
  }
  return sync;
}

double DSP::calcVideoLevel (SSTVMode mode, bool is_adaptive) {
  WindowType win_type;

  if (is_adaptive) win_type = bestWindowFor(mode, calcVideoSNR());
  else             win_type = bestWindowFor(mode);

  double freq = calcPeakFreq(FREQ_BLACK, FREQ_WHITE, win_type);
  return fclipToByte((freq - FREQ_BLACK) / (FREQ_WHITE - FREQ_BLACK));
}

// return: refined peak x position (idx_peak-1 .. idx_peak+1)
double gaussianPeak (const Wave& signal, int idx_peak, bool wrap_around) {
  double y1,y2,y3;
  const int idx_last = signal.size()-1;

  bool was_negative = false;
  if (wrap_around && idx_peak < 0) {
    was_negative = true;
    idx_peak += signal.size();
  }


  if (idx_peak == 0) {
    y1 = signal.at(wrap_around ? idx_last : 1);
    y2 = signal.at(0);
    y3 = signal.at(1);
  } else if (idx_peak == idx_last) {
    y1 = signal.at(idx_last-1);
    y2 = signal.at(idx_last);
    y3 = signal.at(wrap_around ? 0 : idx_last-1);
  } else {
    y1 = signal.at(idx_peak - 1);
    y2 = signal.at(idx_peak);
    y3 = signal.at(idx_peak + 1);
  }

  double refined = idx_peak;
  if (was_negative)
    refined -= (int)signal.size();

  if (2*y2 - y3 - y1 != 0.0) {
    double offset = ((y3 - y1) / (2 * (2*y2 - y3 - y1)));
    refined += offset;
  }

  return refined;
}

/*WindowType DSPworker::bestWindowFor(SSTVMode Mode) {
  return bestWindowFor(Mode, 20);
}*/

namespace window {
  Wave Hann (int winlen) {
    Wave result(winlen);
    for (int i=0; i < winlen; i++)
      result[i] = 0.5 * (1 - cos( (2 * M_PI * i) / winlen) );
    return result;
  }

  Wave Blackmann (int winlen) {
    Wave result(winlen);
    for (int i=0; i < winlen; i++)
      result[i] = 0.42 - 0.5*cos(2*M_PI*i/winlen) - 0.08*cos(4*M_PI*i/winlen);

    return result;
  }

  Wave Rect (int winlen) {
    Wave result(winlen);
    double sigma = 0.4;
    for (int i=0; i < winlen; i++)
      result[i] = exp(-0.5*((i-(winlen-1)/2)/(sigma*(winlen-1)/2)));

    return result;
  }

  Wave Gauss (int winlen) {
    Wave result(winlen);
    for (int i=0; i < winlen; i++)
      result[i] = 1;

    return result;
  }
}

double sinc (double x) {
  return (x == 0.0 ? 1 : sin(M_PI*x) / (M_PI*x));
}

namespace kernel {
  Wave Lanczos (size_t kernel_len, size_t a) {
    Wave kern(kernel_len);
    for (size_t i=0; i<kernel_len; i++) {
      double x_kern = (1.0*i/(kernel_len-1) - .5)*2*a;
      double x_wind = 2.0*i/(kernel_len-1) - 1;
      kern[i] = sinc(x_kern) * sinc(x_wind);
    }
    return kern;
  }

  Wave Tent (size_t kernel_len) {
    Wave kern(kernel_len);
    for (size_t i=0; i<kernel_len; i++) {
      double x = 1.0*i/(kernel_len-1);
      kern[i] = 1-2*fabs(x-0.5);
    }
    return kern;
  }
}

double complexMag (fftw_complex coeff) {
  return sqrt(pow(coeff[0],2) + pow(coeff[1],2));
}


Wave convolve (const Wave& sig, const Wave& kernel, bool wrap_around) {

  assert (kernel.size() % 2 == 1);

  Wave result(sig.size());

  for (size_t i=0; i<sig.size(); i++) {

    for (size_t i_kern=0; i_kern<kernel.size(); i_kern++) {
      int i_new = i - kernel.size()/2  + i_kern;
      if (wrap_around) {
        if (i_new < 0)
          i_new += result.size();
        result[i_new % result.size()] += sig[i] * kernel[i_kern];
      } else {
        if (i_new >= 0 && i_new <= int(result.size()-1))
          result[i_new] += sig[i] * kernel[i_kern];
      }
    }

  }

  return result;
}

Wave upsample (const Wave& orig, size_t factor, int kern_type) {

  Wave kern;
  if (kern_type == KERNEL_LANCZOS2) {
    kern = kernel::Lanczos(factor*2*2 + 1, 2);
  } else if (kern_type == KERNEL_LANCZOS3) {
    kern = kernel::Lanczos(factor*3*2 + 1, 3);
  } else if (kern_type == KERNEL_TENT) {
    kern = kernel::Tent(factor*2 + 1);
  }

  size_t orig_size = orig.size();

  Wave padded(orig_size * factor);
  for (size_t i=0; i<orig_size; i++) {
    padded[i * factor] = orig[i];
  }
  padded.insert(padded.begin(), factor-1, 0);
  padded.insert(padded.begin(), orig[0]);
  padded.push_back(orig[orig_size-1]);

  Wave filtered = convolve(padded, kern);

  filtered.erase(filtered.begin(), filtered.begin()+factor/2);
  filtered.erase(filtered.end()-factor/2, filtered.end());

  return filtered;
}

Wave deriv (const Wave& wave) {
  Wave result;
  for (size_t i=1; i<wave.size(); i++)
    result.push_back(wave[i] - wave[i-1]);
  return result;
}

std::vector<double> peaks (const Wave& wave, size_t n) {
  std::vector<std::pair<double,double> > peaks;
  for (size_t i=0; i<wave.size(); i++) {
    double y1 = (i==0 ? wave[0] : wave[i-1]);
    double y2 = wave[i];
    double y3 = (i==wave.size()-1 ? wave[wave.size()-1] : wave[i+1]);
    if ( fabs(y2) >= fabs(y1) && fabs(y2) >= fabs(y3) )
      peaks.push_back({ i + gaussianPeak(y1, y2, y3), wave[i]});
  }
  std::sort(peaks.begin(), peaks.end(),
    [](std::pair<double,double> a, std::pair<double,double> b) {
      return fabs(b.second) < fabs(a.second);
    });

  Wave result;
  for (int i=0;i<n && i<(int)peaks.size(); i++)
    result.push_back(peaks[i].first);

  std::sort(result.begin(), result.end());

  return result;
}


std::vector<double> derivPeaks (const Wave& wave, int n) {
  std::vector<double> result = peaks(deriv(wave), n);
  for (int i=0; i<(int)result.size(); i++) {
    result[i] += .5;
  }
  return result;
}

/* pass:     wave      FM demodulated signal
 *           melody    array of Tones
 *                     (zero frequency to accept any)
 *           dt        sample delta
 *
 * returns:  (bool)    did we find it
 *           (double)  at which frequency shift
 *           (double)  started how many seconds before the last sample
 */
std::tuple<bool,double,double> findMelody (const Wave& wave, const Melody& melody, double dt, double min_shift, double max_shift) {
  bool   was_found = true;
  int    start_at = 0;
  double avg_fdiff = 0.0;
  const double freq_margin = 25.0;
  double tshift = 0.0;
  double t = melody[melody.size()-1].dur;
  std::vector<double> fdiffs;

  for (int i=melody.size()-2; i>=0; i--)  {
    if (melody[i].freq != 0.0) {
      double delta_f_ref = melody[i].freq - melody[melody.size()-1].freq;
      double delta_f     = wave[wave.size()-1 - (t/dt)] - wave[wave.size()-1];
      double err_f       = delta_f - delta_f_ref;
      was_found = was_found && (fabs(err_f) < freq_margin);
    }
    start_at = wave.size() - (t / dt);
    t += melody[i].dur;
  }

  if (was_found) {

    /* refine fshift */
    int melody_i = 0;
    double next_tone_t = melody[melody_i].dur;
    for (int i=start_at; i<(int)wave.size(); i++) {
      double fref = melody[melody_i].freq;
      double fdiff = (wave[i] - fref);
      fdiffs.push_back(fdiff);
      if ( (i-start_at)*dt >= next_tone_t ) {
        melody_i ++;
        next_tone_t += melody[melody_i].dur;
      }
    }
    std::sort(fdiffs.begin(), fdiffs.end());
    avg_fdiff = fdiffs[fdiffs.size()/2];

    /* refine start_at */
    Wave subwave(wave.begin()+start_at, wave.end());
    Wave edges_rx = derivPeaks(subwave, melody.size()-1);
    Wave edges_ref;
    double t = 0.0;
    for (int i=0; i<(int)melody.size()-1; i++) {
      t += melody[i].dur;
      edges_ref.push_back(t);
    }

    tshift = 0.0;
    if (edges_rx.size() == edges_ref.size()) {
      for (int i=0; i<(int)edges_rx.size(); i++) {
        tshift += (edges_rx[i]*dt - edges_ref[i]);
      }
      tshift = start_at*dt + (tshift / edges_rx.size()) - ((wave.size()-1)*dt);
    } else {
      // can't refine
      tshift = start_at*dt - ((wave.size()-1)*dt);
    }

  }

  if (avg_fdiff < min_shift || avg_fdiff > max_shift)
    was_found = false;


  return { was_found, avg_fdiff, tshift };
}

void DSP::set_fshift (double fshift) {
  m_fshift = fshift;
}

int DSP::freq2bin (double freq, int fft_len) const {
  return ((freq-m_freq_if) / m_input->getSamplerate() * fft_len * m_decim_ratio);
}

