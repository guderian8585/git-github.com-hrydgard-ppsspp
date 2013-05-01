// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include "TextureScaler.h"

#include "Core/Config.h"
#include "Common/Log.h"
#include "Common/MsgHandler.h"
#include "Common/CommonFuncs.h"
#include "ext/xbrz/xbrz.h"

WorkerThread::WorkerThread() : active(true), started(false) {
	thread = new std::thread([&]() { WorkFunc(); });
	doneMutex.lock();
	while(!started) { };
}

WorkerThread::~WorkerThread() {
	mutex.lock();
	active = false;
	signal.notify_one();
	mutex.unlock();
	thread->join();
	delete thread;
}

void WorkerThread::Process(const std::function<void()>& work) {
	mutex.lock();
	work_ = work;
	signal.notify_one();
	mutex.unlock();
}

void WorkerThread::WaitForCompletion() {
	done.wait(doneMutex);
}

void WorkerThread::WorkFunc() {
	mutex.lock();
	started = true;
	while(active) {
		signal.wait(mutex);
		if(active) work_();
		doneMutex.lock();
		done.notify_one();
		doneMutex.unlock();
	}
}


TextureScaler::TextureScaler() : numThreads(4), workersStarted(false) {
}

void TextureScaler::StartWorkers() {
	if(!workersStarted) {
		for(int i=0; i<numThreads; ++i) {
			workers.push_back(std::make_shared<WorkerThread>());
		}
		workersStarted = true;
	}
}

void TextureScaler::ParallelLoop(std::function<void(int,int)> loop, int lower, int upper) {
	StartWorkers();
	int range = upper-lower;
	if(range >= numThreads*2) { // don't parallelize tiny loops
		// could do slightly better load balancing for the generic case, 
		// but doesn't matter since all our loops are power of 2
		int chunk = range/numThreads; 
		for(int s=lower, i=0; i<numThreads; s+=chunk, ++i) {
			workers[i]->Process(std::bind(loop, s, std::min(s+chunk,upper)));
		}
		for(int i=0; i<numThreads; ++i) {
			workers[i]->WaitForCompletion();
		}
	} else {
		loop(lower, upper);
	}
}

//#define SCALING_MEASURE_TIME

#ifdef SCALING_MEASURE_TIME
#include "native/base/timeutil.h"
#endif

void TextureScaler::Scale(u32* &data, GLenum &dstFmt, int &width, int &height) {
	if(g_Config.iXBRZTexScalingLevel > 1) {
		#ifdef SCALING_MEASURE_TIME
		double t_start = real_time_now();
		#endif

		int factor = g_Config.iXBRZTexScalingLevel;

		// depending on the factor and texture sizes, these can be pretty large (25 MB for a 512 by 512 texture with scaling factor 5)
		bufInput.resize(width*height); // used to store the input image image if it needs to be reformatted
		bufOutput.resize(width*height*factor*factor); // used to store the upscaled image
		u32 *xbrzInputBuf = bufInput.data();
		u32 *xbrzBuf = bufOutput.data();

		// convert texture to correct format for xBRZ
		switch(dstFmt) {
		case GL_UNSIGNED_BYTE:
			xbrzInputBuf = data; // already fine
			break;

		case GL_UNSIGNED_SHORT_4_4_4_4:
			ParallelLoop([&](int l, int u){
				for(int y = l; y < u; ++y) {
					for(int x = 0; x < width; ++x) {
						u32 val = ((u16*)data)[y*width + x];
						u32 r = ((val>>12) & 0xF) * 17;
						u32 g = ((val>> 8) & 0xF) * 17;
						u32 b = ((val>> 4) & 0xF) * 17;
						u32 a = ((val>> 0) & 0xF) * 17;
						xbrzInputBuf[y*width + x] = (a << 24) | (b << 16) | (g << 8) | r;
					}
				}
			}, 0, height);
			break;

		case GL_UNSIGNED_SHORT_5_6_5:
			ParallelLoop([&](int l, int u){
				for(int y = l; y < u; ++y) {
					for(int x = 0; x < width; ++x) {
						u32 val = ((u16*)data)[y*width + x];
						u32 r = ((val>>11) & 0x1F) * 8;
						u32 g = ((val>> 5) & 0x3F) * 4;
						u32 b = ((val    ) & 0x1F) * 8;
						xbrzInputBuf[y*width + x] = (0xFF << 24) | (b << 16) | (g << 8) | r;
					}
				}
			}, 0, height);
			break;

		case GL_UNSIGNED_SHORT_5_5_5_1:
			ParallelLoop([&](int l, int u) {
				for(int y = l; y < u; ++y) {
					for(int x = 0; x < width; ++x) {
						u32 val = ((u16*)data)[y*width + x];
						u32 r = ((val>>11) & 0x1F) * 8;
						u32 g = ((val>> 6) & 0x1F) * 8;
						u32 b = ((val>> 1) & 0x1F) * 8;
						u32 a = (val & 0x1) * 255;
						xbrzInputBuf[y*width + x] = (a << 24) | (b << 16) | (g << 8) | r;
					}
				}
			}, 0, height);
			break;

		default:
			ERROR_LOG(G3D, "iXBRZTexScaling: unsupported texture format");
		}

		// scale 
		xbrz::ScalerCfg cfg;
		ParallelLoop([&](int l, int u) {
			xbrz::scale(factor, xbrzInputBuf, xbrzBuf, width, height, cfg, l, u);
		}, 0, height);

		// update values accordingly
		data = xbrzBuf;
		dstFmt = GL_UNSIGNED_BYTE;
		width *= factor;
		height *= factor;

		#ifdef SCALING_MEASURE_TIME
		if(width*height > 64*64*factor*factor) {
			double t = real_time_now() - t_start;
			NOTICE_LOG(MASTER_LOG, "TextureScaler: processed %9d pixels in %6.5lf seconds. (%9.0lf Mpixels/second)", 
				width*height, t, (width*height)/(t*1000*1000));
		}
		#endif
	}
}
