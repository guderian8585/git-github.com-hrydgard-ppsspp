//Modified to use in PPSSPP, Grabbed from:
//http://forums.ngemu.com/showthread.php?t=76098

// Advanced Cartoon shader I
// by guest(r) (guest.r@gmail.com)
// license: GNU-GPL

#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

const float bb = 0.5; // effects black border sensitivity; from 0.0 to 1.0

uniform sampler2D sampler0;

void main()
{
	vec3 c00 = texture2D(sampler0, gl_TexCoord[5].xy).xyz; 
	vec3 c10 = texture2D(sampler0, gl_TexCoord[1].xy).xyz; 
	vec3 c20 = texture2D(sampler0, gl_TexCoord[2].zw).xyz; 
	vec3 c01 = texture2D(sampler0, gl_TexCoord[3].xy).xyz; 
	vec3 c11 = texture2D(sampler0, gl_TexCoord[0].xy).xyz; 
	vec3 c21 = texture2D(sampler0, gl_TexCoord[4].xy).xyz; 
	vec3 c02 = texture2D(sampler0, gl_TexCoord[1].zw).xyz; 
	vec3 c12 = texture2D(sampler0, gl_TexCoord[2].xy).xyz; 
	vec3 c22 = texture2D(sampler0, gl_TexCoord[6].xy).xyz; 
	vec3 dt = vec3(1.0,1.0,1.0); 

	float d1=dot(abs(c00-c22),dt);
	float d2=dot(abs(c20-c02),dt);
	float hl=dot(abs(c01-c21),dt);
	float vl=dot(abs(c10-c12),dt);
	float d = bb*(d1+d2+hl+vl)/(dot(c11,dt)+0.15);

	float lc = 4.0*length(c11);
	float f = fract(lc); f*=f;
	lc = 0.25*(floor(lc) + f*f)+0.05;
	c11 = 4.0*normalize(c11); 
	vec3 frct = fract(c11); frct*=frct;
	c11 = floor(c11)+ 0.05*dt + frct*frct;
	gl_FragColor.xyz = 0.25*lc*(1.1-d*sqrt(d))*c11;
}
