/*
 *  Copyright (c) 2003-2010, Mark Borgerding. All rights reserved.
 *  This file is part of KISS FFT - https://github.com/mborgerding/kissfft
 *  SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef kiss_fft_log_h
#define kiss_fft_log_h
#define KISS_FFT_LOG_MSG(severity, ...) ((void)0)
#define KISS_FFT_ERROR(...)   KISS_FFT_LOG_MSG(ERROR, __VA_ARGS__)
#define KISS_FFT_WARNING(...) KISS_FFT_LOG_MSG(WARNING, __VA_ARGS__)
#define KISS_FFT_INFO(...)    KISS_FFT_LOG_MSG(INFO, __VA_ARGS__)
#define KISS_FFT_DEBUG(...)   KISS_FFT_LOG_MSG(DEBUG, __VA_ARGS__)
#endif
