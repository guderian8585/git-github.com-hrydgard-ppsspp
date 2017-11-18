package org.ppsspp.ppsspp;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

import android.graphics.Point;
import android.opengl.GLES20;
import android.opengl.GLSurfaceView;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.Display;

public class NativeRenderer implements GLSurfaceView.Renderer {
	private static String TAG = "NativeRenderer";
	private NativeActivity mActivity;

	NativeRenderer(NativeActivity act) {
		mActivity = act;
	}

	public void onDrawFrame(GL10 unused /*use GLES20*/) {
		displayRender();
	}

	public void onSurfaceCreated(GL10 unused, EGLConfig config) {
		Log.i(TAG, "NativeRenderer: onSurfaceCreated");
		// Log.i(TAG, "onSurfaceCreated - EGL context is new or was lost");
		// Actually, it seems that it is here we should recreate lost GL objects.
		displayInit();
	}
 
	public void onSurfaceChanged(GL10 unused, int width, int height) {
	}

	// Note: This also means "device lost" and you should reload
	// all buffered objects. 
	public native void displayInit(); 
	public native void displayRender();
}
