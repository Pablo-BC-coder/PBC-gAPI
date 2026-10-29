// PBC-gAPI — Copyright (c) Pablo B. C. All rights reserved.
#include "DisplayAdapter.h"
#include <PBCgAPI.h>
#include <PBCMeshes.h>
#include <esp_heap_caps.h>

static_assert(PBC_RENDER_SCALE==1 || PBC_RENDER_SCALE==2,"Use scale 1 or 2");
static_assert(PBC_STRIP_ROWS>0 && PBC_STRIP_ROWS<=64,"Use 1..64 strip rows");
static PBCDisplay display;
static uint16_t checker[64]; // Exactly 8*8*2 = 128 bytes, generated at setup.
static pbc::Vertex sphere[pbc::SphereVertices],cube[24];
static uint16_t sphereIndices[pbc::SphereIndices],cubeIndices[36];
static pbc::VertexOut transformed[pbc::SphereVertices];
static pbc::BlinnPhong material;
static uint16_t* colorBand=nullptr;
static float* depthBand=nullptr;
static uint16_t* scaledRow=nullptr;
static int renderWidth=0,renderHeight=0;
static bool sphereSelected=false,initialized=false,changePending=false;
static float angle=0;
static uint32_t lastFrame=0,lastReport=0,frames=0,renderMicros=0,transferMicros=0;

// User-programmable vertex and fragment stages: replace either body freely.
struct ExampleVertexShader {
    pbc::TransformShader transform;
    pbc::VertexOut operator()(const pbc::Vertex& vertex) const { return transform(vertex); }
};
struct ExampleFragmentShader {
    pbc::Color operator()(const pbc::VaryData& varying,const pbc::Framebuffer& fb) const {
        return material(varying,fb);
    }
};
static void changeModel() {
    changePending=!changePending;
}
static void pollSerial() {
    static char line[32]; static unsigned length=0;
    static bool overflow=false; static uint32_t lastByte=0;
    bool received=false;
    while(Serial.available()) {
        const char c=char(Serial.read()); lastByte=millis(); received=true;
        if(c=='\n'||c=='\r') {
            if(!overflow && length) {
                line[length]='\0'; if(strcmp(line,"change")==0) changeModel();
            }
            length=0; overflow=false;
        } else if(length<sizeof(line)-1 && !overflow) line[length++]=c;
        else overflow=true;
    }
    // Also accept Monitor Serial configured with "No line ending".
    if(!received && (length||overflow) && uint32_t(millis()-lastByte)>120) {
        line[length]='\0'; if(!overflow && strcmp(line,"change")==0) changeModel();
        length=0; overflow=false;
    }
}
static void pollTouch() {
    static bool candidate=false,stable=false;
    static uint32_t changed=0,lastSample=0;
    const uint32_t now=millis();
    if(uint32_t(now-lastSample)<10) return;
    lastSample=now;
    const bool down=pbcTouched(display);
    if(down!=candidate) {candidate=down;changed=now;}
    if(candidate!=stable && uint32_t(now-changed)>=30) {
        stable=candidate; if(stable) changeModel();
    }
}
static void presentBand(int y,int rows) {
    display.startWrite();
#if PBC_RENDER_SCALE==1
    // Blocking transfer: buffers may be reused immediately after return.
    display.pushImage(0,y,renderWidth,rows,colorBand);
#else
    for(int row=0;row<rows;++row) {
        for(int x=0;x<renderWidth;++x)
            scaledRow[2*x]=scaledRow[2*x+1]=colorBand[row*renderWidth+x];
        display.pushImage(0,(y+row)*2,renderWidth*2,1,scaledRow);
        display.pushImage(0,(y+row)*2+1,renderWidth*2,1,scaledRow);
    }
#endif
    display.endWrite();
}
void setup() {
    Serial.begin(115200);
#if PBC_TOUCH_4WIRE
    analogReadResolution(12);
#endif
    display.init(); display.setRotation(1);
#if !PBC_USE_TFT_ESPI
    display.setColorDepth(16);
#endif
    display.setSwapBytes(true); // Buffers store native numeric RGB565, not pre-swapped.
    display.fillScreen(0xFFFF);
    renderWidth=display.width()/PBC_RENDER_SCALE;
    renderHeight=display.height()/PBC_RENDER_SCALE;
    if(renderWidth<=0||renderWidth>1024||renderHeight<=0||renderHeight>1024) {
        Serial.println("ERRO: dimensoes invalidas"); return;
    }
    const size_t pixels=size_t(renderWidth)*PBC_STRIP_ROWS;
    colorBand=static_cast<uint16_t*>(heap_caps_malloc(pixels*sizeof(uint16_t),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
    depthBand=static_cast<float*>(heap_caps_malloc(pixels*sizeof(float),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
#if PBC_RENDER_SCALE==2
    scaledRow=static_cast<uint16_t*>(heap_caps_malloc(display.width()*sizeof(uint16_t),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
#endif
    if(!colorBand||!depthBand||(PBC_RENDER_SCALE==2&&!scaledRow)) {
        free(colorBand);free(depthBand);free(scaledRow);
        colorBand=nullptr;depthBand=nullptr;scaledRow=nullptr;
        Serial.println("ERRO: RAM insuficiente; reduza PBC_STRIP_ROWS"); return;
    }
    pbc::makeChecker8x8(checker);
    pbc::makeCube(cube,cubeIndices); pbc::makeSphere16(sphere,sphereIndices);
    material.texture={checker,8,8};
    material.directionalIntensity=1.0f; material.ambientIntensity=0.5f;
    material.toLight=pbc::normalize({-0.4f,0.6f,1.0f});
    material.specularIntensity=0.30f; material.shininess=32;
    initialized=true; lastFrame=lastReport=millis();
    Serial.printf("PBC-gAPI: %dx%d -> TFT %dx%d; faixas %d; heap livre %u\n",
        renderWidth,renderHeight,display.width(),display.height(),PBC_STRIP_ROWS,ESP.getFreeHeap());
    Serial.println("Toque ou envie change (115200 baud) para alternar cubo/esfera.");
}
void loop() {
    if(!initialized) {delay(100);return;}
    // Freeze mesh and transforms for the entire frame. Input is applied at
    // frame boundaries; no half-cube/half-sphere when an event arrives mid-frame.
    pollSerial(); pollTouch();
    if(changePending) {
        sphereSelected=!sphereSelected; changePending=false;
        // Continuous angle; only the angular velocity changes sign.
        Serial.println(sphereSelected?"Esfera 16x16: normais radiais, giro negativo":"Cubo: normais por face, giro positivo");
    }
    const uint32_t now=millis();
    const float dt=uint32_t(now-lastFrame)*0.001f; lastFrame=now;
    angle=std::fmod(angle+(sphereSelected?-1.0f:1.0f)*dt,2*pbc::kPi);
    const pbc::Vertex* vertices=sphereSelected?sphere:cube;
    const uint16_t* indices=sphereSelected?sphereIndices:cubeIndices;
    const size_t count=sphereSelected?pbc::SphereVertices:24;
    const size_t indexCount=sphereSelected?pbc::SphereIndices:36;
    ExampleVertexShader vs;
    vs.transform.modelView=pbc::Mat4::translation(0,0,-3.6f)*
        pbc::Mat4::rotationX(0.35f)*pbc::Mat4::rotationY(angle);
    vs.transform.projection=pbc::Mat4::perspective(50*pbc::kPi/180,
        float(renderWidth)/renderHeight,0.1f,20.0f);
    const uint32_t vertexStart=micros();
    pbc::shadeVertices(vertices,transformed,count,vs);
    renderMicros+=uint32_t(micros()-vertexStart);
    pbc::Framebuffer fb(renderWidth,renderHeight,PBC_STRIP_ROWS,colorBand,depthBand);
    for(int y=0;y<renderHeight;y+=PBC_STRIP_ROWS) {
        const int rows=std::min(PBC_STRIP_ROWS,renderHeight-y);
        uint32_t start=micros();
        fb.beginBand(y,rows,0xFFFF);
        pbc::drawIndexed(transformed,count,indices,indexCount,fb,ExampleFragmentShader{},true);
        renderMicros+=uint32_t(micros()-start);
        start=micros(); presentBand(y,rows); transferMicros+=uint32_t(micros()-start);
        pollSerial(); pollTouch(); // Queue events; commit them at the next frame.
        delay(0); // Yield to the ESP32 scheduler between bands.
    }
    ++frames;
    const uint32_t elapsed=uint32_t(millis()-lastReport);
    if(elapsed>=2000) {
        Serial.printf("%s: %.2f FPS | CPU %.2f ms | TFT %.2f ms | heap %u | maior bloco %u\n",
            sphereSelected?"esfera":"cubo",1000.0f*frames/elapsed,
            renderMicros/(1000.0f*frames),transferMicros/(1000.0f*frames),
            ESP.getFreeHeap(),heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
        frames=renderMicros=transferMicros=0; lastReport=millis();
    }
}
