// ==================================================================================
// Copyright (c) 2012 HiFi-LoFi
//
// This is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
// ==================================================================================

#ifndef _CONVOLVER_H
#define _CONVOLVER_H

// We need to include this before the Juce includes due to some
// name clashes with Apple system headers, for more information see:
// http://www.juce.com/forum/topic/reference-point-ambiguous
//#include "FFTConvolver/TwoStageFFTConvolver.h"
#include "TwoStageFFTConvolver.h"
#include <juce_core/juce_core.h>
#include <atomic>

// Taken from KlangFalter via REEV-R (tiagolr/reevr): tail convolution runs on a background thread.
class Convolver : public fftconvolver::TwoStageFFTConvolver
{
public:
  Convolver();
  ~Convolver() override;
  bool isFinished();
  /** Blocks until a pending background tail job has finished (call before init/reset from a non-audio thread). */
  void waitIdle() { _backgroundProcessingFinishedEvent.wait(); }

protected:
  void startBackgroundProcessing() override;
  void waitForBackgroundProcessing() override;

private:
  friend class ConvolverBackgroundThread;

  std::unique_ptr<juce::Thread> _thread;
  std::atomic<juce::uint32> _backgroundProcessingFinished;
  juce::WaitableEvent _backgroundProcessingFinishedEvent;
};


#endif // Header guard
