/*{
  "ISFVSN": "2",
  "DESCRIPTION": "Absolute camera/reference difference mask. Capture a reference; optionally compensate global lighting. Pixel metrics belong to Interactions > Pixels.",
  "CREDIT": "Pixel Stereo",
  "CATEGORIES": [
    "Analysis",
    "Mask"
  ],
  "INPUTS": [
    {
      "NAME": "inputImage",
      "TYPE": "image"
    },
    {
      "NAME": "channel",
      "LABEL": "Difference channel",
      "TYPE": "long",
      "VALUES": [
        0,
        1,
        2,
        3,
        4,
        5,
        6
      ],
      "LABELS": [
        "Red",
        "Green",
        "Blue",
        "Alpha",
        "Luminance",
        "Chrominance",
        "RGB distance"
      ],
      "DEFAULT": 4
    },
    {
      "NAME": "lighting",
      "LABEL": "Lighting compensation",
      "TYPE": "long",
      "VALUES": [
        0,
        1,
        2
      ],
      "LABELS": [
        "Off",
        "Global offset / white balance",
        "Global offset + exposure"
      ],
      "DEFAULT": 1
    },
    {
      "NAME": "threshold",
      "LABEL": "Noise threshold",
      "TYPE": "float",
      "DEFAULT": 0.05,
      "MIN": 0,
      "MAX": 0.95
    },
    {
      "NAME": "softness",
      "LABEL": "Threshold softness",
      "TYPE": "float",
      "DEFAULT": 0.02,
      "MIN": 0,
      "MAX": 0.3
    },
    {
      "NAME": "outputMode",
      "LABEL": "Mask output",
      "TYPE": "long",
      "VALUES": [
        0,
        1
      ],
      "LABELS": [
        "Graded difference",
        "Soft threshold mask"
      ],
      "DEFAULT": 1
    },
    {
      "NAME": "blur",
      "LABEL": "Spatial blur (pixels)",
      "TYPE": "float",
      "DEFAULT": 0,
      "MIN": 0,
      "MAX": 8
    },
    {
      "NAME": "persistence",
      "LABEL": "Temporal persistence",
      "TYPE": "float",
      "DEFAULT": 0,
      "MIN": 0,
      "MAX": 0.98
    },
    {
      "NAME": "temporalMode",
      "LABEL": "Temporal smoothing",
      "TYPE": "long",
      "VALUES": [
        0,
        1,
        2
      ],
      "LABELS": [
        "Motion trails",
        "Exponential average",
        "Lighten accumulation (hold)"
      ],
      "DEFAULT": 0
    },
    {
      "NAME": "accumulationFrames",
      "LABEL": "Lighten hold frames (0 = until reset)",
      "TYPE": "long",
      "DEFAULT": 32,
      "MIN": 0,
      "MAX": 4096
    },
    {
      "NAME": "invert",
      "TYPE": "bool",
      "DEFAULT": false
    },
    {
      "NAME": "writeAlpha",
      "LABEL": "Write mask to alpha",
      "TYPE": "bool",
      "DEFAULT": false
    },
    {
      "NAME": "capture",
      "LABEL": "Capture reference",
      "TYPE": "event"
    },
    {
      "NAME": "referenceImage",
      "LABEL": "External reference image",
      "TYPE": "image"
    },
    {
      "NAME": "useReferenceImage",
      "LABEL": "Use external reference",
      "TYPE": "bool",
      "DEFAULT": false
    },
    {
      "NAME": "adaptation",
      "LABEL": "Background learning per frame",
      "TYPE": "float",
      "DEFAULT": 0,
      "MIN": 0,
      "MAX": 0.1
    }
  ],
  "PASSES": [
    {
      "TARGET": "currentStats",
      "WIDTH": "1",
      "HEIGHT": "1",
      "FLOAT": true
    },
    {
      "TARGET": "referenceStats",
      "WIDTH": "1",
      "HEIGHT": "1",
      "PERSISTENT": true,
      "FLOAT": true
    },
    {
      "TARGET": "referenceFrame",
      "PERSISTENT": true,
      "FLOAT": true
    },
    {
      "TARGET": "differenceMask"
    },
    {
      "TARGET": "motionTrail",
      "PERSISTENT": true,
      "FLOAT": true
    },
    {}
  ]
}*/

float luminance(vec3 c) { return dot(c,vec3(.2126,.7152,.0722)); }
float chroma(vec3 c) { float y=luminance(c); return clamp(length(vec2((c.b-y)*.5389,(c.r-y)*.6350))*1.41421356,0.0,1.0); }
float scalar(vec4 c) {
    if(channel==0)return c.r;if(channel==1)return c.g;if(channel==2)return c.b;if(channel==3)return c.a;
    if(channel==5)return chroma(c.rgb);return luminance(c.rgb);
}
void sortNine(inout float v[9]) {
    for(int i=1;i<9;i++){
        float x=v[i];int j=i-1;
        for(int k=0;k<9;k++){if(j<0)break;if(v[j]<=x)break;v[j+1]=v[j];j--;}
        v[j+1]=x;
    }
}
vec4 statsCurrent(){
    float r[9];float g[9];float b[9];float l[9];
    for(int y=0;y<3;y++)for(int x=0;x<3;x++){
        int i=y*3+x;vec3 c=IMG_NORM_PIXEL(inputImage,(vec2(float(x),float(y))+.5)/3.0).rgb;
        r[i]=c.r;g[i]=c.g;b[i]=c.b;l[i]=luminance(c);
    }
    sortNine(r);sortNine(g);sortNine(b);sortNine(l);
    return vec4(r[4],g[4],b[4],max(0.0,l[6]-l[2]));
}
vec4 compensate(vec4 c,vec4 current,vec4 reference) {
    if(lighting==0 || channel==3)return c;
    float scale=1.0;
    // Flat backgrounds must retain foreground contrast rather than collapsing it.
    if(lighting==2 && reference.a>.02 && current.a>.02)scale=clamp(reference.a/current.a,.25,4.0);
    c.rgb=clamp((c.rgb-current.rgb)*scale+reference.rgb,0.0,1.0);return c;
}
float difference(vec4 a,vec4 b) {
    float d=channel==6 ? length(a.rgb-b.rgb)*.577350269 : abs(scalar(a)-scalar(b));
    if(outputMode==0)return clamp((d-threshold)/max(.001,1.0-threshold),0.0,1.0);
    float soft=max(.00001,softness);return smoothstep(max(0.0,threshold-soft),threshold+soft,d);
}
float blurredMask(vec2 uv) {
    if(blur<.01)return IMG_NORM_PIXEL(differenceMask,uv).r;
    vec2 step=vec2(blur)/IMG_SIZE(differenceMask);float sum=0.0;
    for(int y=-1;y<=1;y++)for(int x=-1;x<=1;x++)sum+=IMG_NORM_PIXEL(differenceMask,clamp(uv+vec2(float(x),float(y))*step,0.0,1.0)).r;
    return sum/9.0;
}
vec4 outputMask(float m) { if(invert)m=1.0-m;return vec4(vec3(m),writeAlpha?m:1.0); }

vec4 externalStats(){
    float r[9];float g[9];float b[9];float l[9];
    for(int y=0;y<3;y++)for(int x=0;x<3;x++){
        int i=y*3+x;vec3 c=IMG_NORM_PIXEL(referenceImage,(vec2(float(x),float(y))+.5)/3.0).rgb;
        r[i]=c.r;g[i]=c.g;b[i]=c.b;l[i]=luminance(c);
    }
    sortNine(r);sortNine(g);sortNine(b);sortNine(l);
    return vec4(r[4],g[4],b[4],max(0.0,l[6]-l[2]));
}

// Lighten accumulation holds each changed pixel for N quiet frames. For binary
// masks this is the union of motion over the last N frames. Graded masks keep
// their peak until N frames without a hit; zero holds it until capture/reset.
vec4 temporalMask(vec2 uv,bool clearHistory) {
    float cur=blurredMask(uv);
    vec4 state=IMG_NORM_PIXEL(motionTrail,uv);
    if(clearHistory || abs(state.b-float(temporalMode))>.25)state=vec4(0.0);
    if(temporalMode==2){
        float age=cur>.00001?0.0:min(state.g+1.0,65535.0);
        float value=max(cur,state.r);
        if(accumulationFrames>0 && age>=float(accumulationFrames))value=cur;
        return vec4(value,age,2.0,1.0);
    }
    float value=temporalMode==0?max(cur,state.r*persistence):mix(cur,state.r,persistence);
    return vec4(value,0.0,float(temporalMode),1.0);
}

void main(){
    vec2 uv=isf_FragNormCoord;
    bool reset=FRAMEINDEX==0||capture;
    if(PASSINDEX==0){gl_FragColor=statsCurrent();return;}
    if(PASSINDEX==1){
        gl_FragColor=useReferenceImage?externalStats():(reset?IMG_NORM_PIXEL(currentStats,vec2(.5)):IMG_NORM_PIXEL(referenceStats,vec2(.5)));return;
    }
    if(PASSINDEX==2){
        vec4 cur=IMG_NORM_PIXEL(inputImage,uv);
        if(useReferenceImage){gl_FragColor=IMG_NORM_PIXEL(referenceImage,uv);return;}
        if(reset){gl_FragColor=cur;return;}
        vec4 ref=IMG_NORM_PIXEL(referenceFrame,uv);
        vec4 normalized=compensate(cur,IMG_NORM_PIXEL(currentStats,vec2(.5)),IMG_NORM_PIXEL(referenceStats,vec2(.5)));
        // Learn quiet pixels only; default zero keeps a static presence reference.
        float learn=(channel==6?length(normalized.rgb-ref.rgb)*.577350269:abs(scalar(normalized)-scalar(ref)))<threshold?adaptation:0.0;
        gl_FragColor=mix(ref,normalized,learn);return;
    }
    if(PASSINDEX==3){
        vec4 cur=compensate(IMG_NORM_PIXEL(inputImage,uv),IMG_NORM_PIXEL(currentStats,vec2(.5)),IMG_NORM_PIXEL(referenceStats,vec2(.5)));
        float d=reset&&!useReferenceImage?0.0:difference(cur,IMG_NORM_PIXEL(referenceFrame,uv));gl_FragColor=vec4(vec3(d),1.0);return;
    }
    if(PASSINDEX==4){
        gl_FragColor=temporalMask(uv,reset);return;
    }
    gl_FragColor=outputMask(IMG_NORM_PIXEL(motionTrail,uv).r);
}
