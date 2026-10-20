// ============================================================
// 8D Audio Processor v8 — Optimized & Enhanced
// Улучшенный математический HRTF (Convolution) + Все эффекты
// ============================================================

#include <portaudio.h>
#include <GLFW/glfw3.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================
// BIQUAD ФИЛЬТРЫ
// ============================================================

class Biquad {
public:
void setSampleRate(double sr) { sampleRate_ = sr; }
void reset() { z1_ = z2_ = 0.f; }
float process(float x) {
float y = b0_ * x + z1_;
z1_ = b1_ * x - a1_ * y + z2_;
z2_ = b2_ * x - a2_ * y;
return y;
}
void setLPF(float fc) { float w=2.fM_PIfc/sampleRate_; float al=std::sin(w)/(2.f0.7071f); float c=std::cos(w); calc((1-c)0.5f, 1-c, (1-c)0.5f, 1+al, -2c, 1-al); }
void setHPF(float fc) { float w=2.fM_PIfc/sampleRate_; float al=std::sin(w)/(2.f0.7071f); float c=std::cos(w); calc((1+c)0.5f, -(1+c), (1+c)0.5f, 1+al, -2c, 1-al); }
void setLowShelf(float fc, float db) {
float A = std::pow(10.f, db/40.f); float w = 2.fM_PIfc/sampleRate_;
float c = std::cos(w); float al = std::sin(w)0.5fstd::sqrt((A+1/A)(1/1.0f-1)+2);
float tsa = 2.fstd::sqrt(A)al;
calc(A((A+1)-(A-1)c+tsa), 2A*((A-1)-(A+1)c), A((A+1)-(A-1)c-tsa),
(A+1)+(A-1)c+tsa, -2((A-1)+(A+1)c), (A+1)+(A-1)c-tsa);
}
void setPeaking(float fc, float db, float Q) {
float A = std::pow(10.f, db/40.f); float w = 2.fM_PIfc/sampleRate_;
float al = std::sin(w)/(2.fQ); float c = std::cos(w);
calc(1+alA, -2c, 1-alA, 1+al/A, -2c, 1-al/A);
}
private:
void calc(float b0, float b1, float b2, float a0, float a1, float a2) {
b0_=b0/a0; b1_=b1/a0; b2_=b2/a0; a1_=a1/a0; a2_=a2/a0;
}
double sampleRate_=44100.0;
float b0_=1,b1_=0,b2_=0,a1_=0,a2_=0,z1_=0,z2_=0;
};

// ============================================================
// УЛУЧШЕННЫЙ HRTF (ФИЗИЧЕСКАЯ МОДЕЛЬ + СВЕРТКА)
// ============================================================

class EnhancedHRTF {
public:
void setSampleRate(double sr) {
sr_ = sr;
filterLen_ = 64; // Длина FIR-фильтра для имитации уха
hrirL_.resize(filterLen_, 0.f);
hrirR_.resize(filterLen_, 0.f);
histL_.assign(filterLen_, 0.f);
histR_.assign(filterLen_, 0.f);
idx_ = 0;
}

// Генерация HRIR на лету для текущего азимута (угла)
void updateFilters(float azimuth) {
// Нормализуем угол от -PI до PI (0 - спереди, PI/2 - слева)
while (azimuth > M_PI) azimuth -= 2M_PI;
while (azimuth < -M_PI) azimuth += 2M_PI;

// 1. ITD (Задержка между ушами)
// Звук доходит до дальнего уха дольше (макс 0.6 мс)
float itdDelay = std::sin(azimuth) * 0.0006f * sr_; // в сэмплах

// 2. Тень головы (High-frequency attenuation)
// Чем дальше ухо от источника, тем сильнее глушатся высокие частоты
float shadowL = 1.0f, shadowR = 1.0f;
if (azimuth > 0) shadowL = 0.2f; // Источник справа, левое ухо в тени
if (azimuth < 0) shadowR = 0.2f; // Источник слева, правое ухо в тени

// 3. Формирование FIR-фильтров (импульсная характеристика)
for (int i = 0; i < filterLen_; ++i) {
// Базовая задержка (ITD) + окно Ханна
float tL = (i - (azimuth > 0 ? itdDelay : 0)) / (float)filterLen_;
float tR = (i - (azimuth < 0 ? itdDelay : 0)) / (float)filterLen_;

// Тень головы (Low-pass) - аппроксимация через гауссово окно
float gaussL = std::exp(-std::pow(tL * 4.0f, 2));
float gaussR = std::exp(-std::pow(tR * 4.0f, 2));

// Пики ушной раковины (Peaking) - резонанс на 6-8 кГц
float pinnaL = std::sin(tL * M_PI * 4) * 0.1f;
float pinnaR = std::sin(tR * M_PI * 4) * 0.1f;

hrirL_[i] = (gaussL * shadowL + pinnaL) * 0.1f;
hrirR_[i] = (gaussR * shadowR + pinnaR) * 0.1f;
}
}

void process(float in, float& outL, float& outR) {
// Запись в кольцевой буфер
histL_[idx_] = in;
histR_[idx_] = in;

float sumL = 0.f, sumR = 0.f;
// Свёртка (FIR)
for (int i = 0; i < filterLen_; ++i) {
int hidx = (idx_ - i + filterLen_) % filterLen_;
sumL += histL_[hidx] * hrirL_[i];
sumR += histR_[hidx] * hrirR_[i];
}

outL = sumL;
outR = sumR;
idx_ = (idx_ + 1) % filterLen_;
}

private:
double sr_ = 44100.0;
int filterLen_ = 64;
std::vector<float> hrirL_, hrirR_;
std::vector<float> histL_, histR_;
int idx_ = 0;
};

// ============================================================
// ОСТАЛЬНЫЕ ЭФФЕКТЫ (Reverb, Comp, Limiter, DeEsser)
// ============================================================

enum class ReverbMode { Room = 0, Hall = 1, Cave = 2 };

class Reverb {
public:
void setSampleRate(double sr){ sr_=sr; setDelays(0.0297,0.0371,0.0411,0.0437,0.005,0.0017); clear(); }
void setMode(ReverbMode m){
switch(m){ case ReverbMode::Room: fb_=0.6f; damp_=0.45f; break;
case ReverbMode::Hall: fb_=0.78f; damp_=0.3f; break; case ReverbMode::Cave: fb_=0.9f; damp_=0.12f; break; }
}
void clear(){ for(auto& b:comb_) std::fill(b.buf.begin(),b.buf.end(),0); for(auto& b:ap_) std::fill(b.buf.begin(),b.buf.end(),0); }
float process(float in){
float out=0; for(auto& c:comb_){ float d=c.buf[c.i]; c.lp=d*(1-damp_)+c.lpdamp_; c.buf[c.i]=in+c.lpfb_; c.i=(c.i+1)%c.buf.size(); out+=d; }
out*=0.25f; for(auto& a:ap_){ float d=a.buf[a.i]; float y=-in+d; a.buf[a.i]=in+d0.5f; a.i=(a.i+1)%a.buf.size(); in=y; }
return out;
}
private:
struct Comb { std::vector<float> buf; size_t i=0; float lp=0; };
struct Allpass { std::vector<float> buf; size_t i=0; };
void setDelays(double d0,double d1,double d2,double d3,double a0,double a1){
comb_.resize(4); ap_.resize(2);
comb_[0].buf.assign((size_t)(d0sr_),0); comb_[1].buf.assign((size_t)(d1sr_),0);
comb_[2].buf.assign((size_t)(d2sr_),0); comb_[3].buf.assign((size_t)(d3sr_),0);
ap_[0].buf.assign((size_t)(a0sr_),0); ap_[1].buf.assign((size_t)(a1*sr_),0);
}
std::vector<Comb> comb_; std::vector<Allpass> ap_;
double sr_=44100; float fb_=0.78f, damp_=0.3f;
};

class Compressor {
public:
void setSampleRate(double sr){ sr_=sr; }
void set(float th, float r){ th_=th; r_=r; }
void reset(){ env_=0; gain_=1; }
float process(float x){
float a=std::fabs(x); env_ = a > env_ ? a+0.9f*(env_-a) : a+0.99f*(env_-a);
float edb=20std::log10(env_+1e-9f); float odb=edb-th_;
float gdb = odb>0 ? -odb(1-1/r_) : 0; float g=std::pow(10, gdb/20);
gain_ += (g-gain_)0.5f; return xgain_;
}
private:
double sr_=44100; float th_=-18, r_=4; float env_=0, gain_=1;
};

class Limiter {
public:
void setCeiling(float db){ ceil_=std::pow(10,db/20); }
float process(float x){
float y=std::tanh(x/ceil_)*ceil_;
return std::max(-ceil_, std::min(ceil_, y));
}
private:
float ceil_=0.94f;
};

class DeEsser {
public:
void setSampleRate(double sr){ hpf_.setSampleRate(sr); comp_.setSampleRate(sr); hpf_.setCutoff(6000); comp_.set(-22, 4); }
void setAmount(float a){ amount_=a; }
float process(float x){
float hf = hpf_.process(x); float hfC = comp_.process(hf);
return x + (hfC - hf) * amount_;
}
private:
Biquad hpf_; Compressor comp_; float amount_=0.5f;
};

// ============================================================
// AUDIO ENGINE
// ============================================================

class AudioEngine {
public:
AudioEngine()=default; ~AudioEngine(){ stop(); Pa_Terminate(); }
bool init(){ return Pa_Initialize()==paNoError; }

struct DevInfo{ int idx; std::string name; int in, out; };
std::vector<DevInfo> listDevices() const {
std::vector<DevInfo> r; for(int i=0; i<Pa_GetDeviceCount(); ++i){
auto info=Pa_GetDeviceInfo(i); if(!info) continue;
r.push_back({i, info->name?info->name:"Unknown", info->maxInputChannels, info->maxOutputChannels});
} return r;
}

bool start(int in, int out){
if(run_.load()) return false;
sr_=44100.0; phase_=0.0;

revL_.setSampleRate(sr_); revR_.setSampleRate(sr_);
lpfL_.setSampleRate(sr_); lpfR_.setSampleRate(sr_);
hrtfL_.setSampleRate(sr_); hrtfR_.setSampleRate(sr_);
compL_.setSampleRate(sr_); compR_.setSampleRate(sr_);
deessL_.setSampleRate(sr_); deessR_.setSampleRate(sr_);
limL_.setSampleRate(sr_); limR_.setSampleRate(sr_);

itdSize_=(size_t)(0.001sr_); itdL_.assign(itdSize_,0); itdR_.assign(itdSize_,0); itdIdx_=0;
cfSize_=(size_t)(0.0003sr_); cfL_.assign(cfSize_,0); cfR_.assign(cfSize_,0); cfIdx_=0;

PaStreamParameters ip{}; ip.device=in; ip.channelCount=2; ip.sampleFormat=paFloat32; ip.suggestedLatency=Pa_GetDeviceInfo(in)->defaultLowInputLatency;
PaStreamParameters op{}; op.device=out; op.channelCount=2; op.sampleFormat=paFloat32; op.suggestedLatency=Pa_GetDeviceInfo(out)->defaultLowOutputLatency;

PaError e = Pa_OpenStream(&stream_, &ip, &op, sr_, 1024, paClipOff, &AudioEngine::cb, this);
if(e!=paNoError){ stream_=nullptr; return false; }
e = Pa_StartStream(stream_);
if(e!=paNoError){ Pa_CloseStream(stream_); stream_=nullptr; return false; }
run_.store(true); return true;
}

void stop(){ if(stream_){ Pa_StopStream(stream_); Pa_CloseStream(stream_); stream_=nullptr; } run_.store(false); }
bool isRunning() const { return run_.load(); }

void setSpeed(float s){ spd_.store(s); }
void setEnabled(bool e){ en_.store(e); }
void setFig8(bool f){ fig8_.store(f); }
void setItd(bool e){ itd_.store(e); }
void setHrtf(bool e){ hrtf_.store(e); }
void setFilt(bool e){ filt_.store(e); }
void setRev(bool e){ rev_.store(e); }
void setComp(bool e){ comp_.store(e); }
void setWidth(bool e){ widEn_.store(e); }
void setBass(float db){ bass_.store(db); }
void setDynEq(bool e){ dyneq_.store(e); }
void setLim(bool e){ lim_.store(e); }
void setCrossfeed(bool e){ cfEn_.store(e); }
void setDeEsser(bool e){ dsEn_.store(e); }

float getLevelL() const { return lvlL_.load(); }
float getLevelR() const { return lvlR_.load(); }

std::atomic<bool> revEn_{false}, widEn_{false}, dyneq_{false}, cfEn_{true}, dsEn_{true};
std::atomic<float> wid_{1.3f}, bass_{0.0f}, cfAmt_{0.3f}, dsAmt_{0.5f};
std::atomic<ReverbMode> revMode_{ReverbMode::Hall};

private:
static int cb(const void* i, void* o, unsigned long f, const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags, void* u){
return static_cast<AudioEngine*>(u)->process((const float*)i, (float*)o, f);
}

int process(const float* in, float* out, unsigned long frames){
bool en = en_.load(); float spd = spd_.load();
bool useItd = itd_.load(); bool useHrtf = hrtf_.load();
bool useFilt = filt_.load(); bool useRev = rev_.load();
bool useWid = widEn_.load(); float width = wid_.load();
float bDb = bass_.load(); bool useLim = lim_.load();
bool useFig8 = fig8_.load();
bool useCf = cfEn_.load(); float cfAmt = cfAmt_.load();
bool useDs = dsEn_.load(); float dsAmt = dsAmt_.load();

revL_.setMode(revMode_.load()); revR_.setMode(revMode_.load());
bassL_.setLowShelf(100, bDb); bassR_.setLowShelf(100, bDb);
deessL_.setAmount(dsAmt); deessR_.setAmount(dsAmt);

float pkL=0, pkR=0; double dp = 2.0M_PIspd/sr_;

for(unsigned long i=0; i<frames; ++i){
float inL=in[i*2], inR=in[i*2+1];
float m = 0.5f*(inL+inR);
float yL, yR;

if(!en){ yL=inL; yR=inR; }
else {
float cosA, sinA;
if(useFig8) {
float x = std::sin(phase_); float y = std::sin(phase_) * std::cos(phase_);
cosA = x; sinA = y;
} else {
cosA = std::cos(phase_); sinA = std::sin(phase_);
}

if(useHrtf) {
// Обновляем фильтры только раз в 32 сэмпла для экономии CPU!
if (hrtfCounter_++ % 32 == 0) {
hrtfL_.updateFilters(phase_);
hrtfR_.updateFilters(phase_); // Симметрия
}
hrtfL_.process(m, yL, yR);
} else {
yL = m * cosA * 0.707f;
yR = m * sinA * 0.707f;
}

phase_ += dp; if(phase_ > 2M_PI) phase_ -= 2M_PI;

if(useItd){
size_t d = (size_t)((1.f-std::fabs(cosA))0.0005fsr_);
d = std::min(d, itdSize_-1); size_t ri=(itdIdx_+itdSize_-d)%itdSize_;
itdL_[itdIdx_]=yL; itdR_[itdIdx_]=yR; yL=itdL_[ri]; yR=itdR_[ri]; itdIdx_=(itdIdx_+1)%itdSize_;
}

if(useFilt){
float b = std::max(0.f,-cosA);
if(b>0){ yL = lpfL_.process(yL); yR = lpfR_.process(yR); }
}

if(useRev && revEn_.load()){
float wL=revL_.process(yL), wR=revR_.process(yR);
yL=yL0.7f + wL0.3f; yR=yR0.7f + wR0.3f;
}

yL = bassL_.process(yL); yR = bassR_.process(yR);

if(comp_.load()){ yL=compL_.process(yL); yR=compR_.process(yR); }

if(useWid){
float mid=0.5f*(yL+yR), side=0.5f*(yL-yR)*width;
yL = mid+side; yR = mid-side;
}

if(useCf){
cfL_[cfIdx_]=yL; cfR_[cfIdx_]=yR;
float dL=cfL_[cfIdx_]; float dR=cfR_[cfIdx_];
cfIdx_=(cfIdx_+1)%cfSize_;
yL = yL*(1.f-0.5fcfAmt) + dR0.5fcfAmt;
yR = yR(1.f-0.5fcfAmt) + dL0.5f*cfAmt;
}

if(useDs){ yL=deessL_.process(yL); yR=deessR_.process(yR); }

if(useLim){ yL=limL_.process(yL); yR=limR_.process(yR); }
}

out[i*2]=std::max(-0.99f,std::min(0.99f,yL));
out[i*2+1]=std::max(-0.99f,std::min(0.99f,yR));
pkL=std::max(pkL,std::fabs(yL)); pkR=std::max(pkR,std::fabs(yR));
}
lvlL_.store(lvlL_.load()0.7f+pkL0.3f); lvlR_.store(lvlR_.load()0.7f+pkR0.3f);
return paContinue;
}

PaStream* stream_=nullptr;
std::atomic<bool> run_{false}, en_{true}, fig8_{false}, itd_{false}, hrtf_{false};
std::atomic<bool> filt_{false}, rev_{false}, comp_{false}, lim_{false};
std::atomic<float> spd_{0.15f}, lvlL_{0}, lvlR_{0};

double sr_=44100, phase_=0;
int hrtfCounter_ = 0;
std::vector<float> itdL_, itdR_; size_t itdSize_=0, itdIdx_=0;
std::vector<float> cfL_, cfR_; size_t cfSize_=0, cfIdx_=0;

Biquad lpfL_, lpfR_, bassL_, bassR_;
EnhancedHRTF hrtfL_, hrtfR_;
Reverb revL_, revR_;
Compressor compL_, compR_;
DeEsser deessL_, deessR_;
Limiter limL_, limR_;
};

// ============================================================
// GUI
// ============================================================

static void glfwErr(int e, const char* d){ std::fprintf(stderr,"GLFW Error %d: %s\n",e,d); }
static void ApplyTheme(){
ImGuiStyle& s=ImGui::GetStyle();
s.WindowPadding=ImVec2(16,16); s.FramePadding=ImVec2(10,6);
s.ItemSpacing=ImVec2(10,8); s.WindowRounding=8; s.FrameRounding=6;
ImVec4* c=s.Colors;
c[ImGuiCol_WindowBg]=ImVec4(0.09f,0.10f,0.13f,1); c[ImGuiCol_FrameBg]=ImVec4(0.15f,0.17f,0.22f,1);
c[ImGuiCol_Button]=ImVec4(0.20f,0.23f,0.30f,1); c[ImGuiCol_ButtonHovered]=ImVec4(0.28f,0.32f,0.42f,1);
c[ImGuiCol_CheckMark]=ImVec4(0.45f,0.65f,1,1); c[ImGuiCol_SliderGrab]=ImVec4(0.45f,0.65f,1,1);
}

int main(){
glfwSetErrorCallback(glfwErr); if(!glfwInit()) return 1;
glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,2);
glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE); glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT,GL_TRUE);

GLFWwindow* win = glfwCreateWindow(950, 920, "8D Audio Processor v8", nullptr, nullptr);
if(!win) return 1; glfwMakeContextCurrent(win); glfwSwapInterval(1);

IMGUI_CHECKVERSION(); ImGui::CreateContext(); ApplyTheme();
ImGui_ImplGlfw_InitForOpenGL(win,true); ImGui_ImplOpenGL3_Init("#version 150");

AudioEngine audio; if(!audio.init()) return 1;

auto devs = audio.listDevices();
std::vector<std::string> inN, outN; std::vector<int> inI, outI;
for(auto& d:devs){ if(d.in>=2){inN.push_back(d.name); inI.push_back(d.idx);} if(d.out>=2){outN.push_back(d.name); outI.push_back(d.idx);} }

int sIn=0, sOut=0, mode=0;
float rotSpd=0.15f, bassDb=0.0f, widthVal=1.3f, cfVal=0.3f, dsVal=0.5f;
bool itdOn=true, hrtfOn=false, filtOn=true, revOn=false, compOn=true, widOn=true, limOn=true, fig8On=false, dyneqOn=false, cfOn=true, dsOn=true;
char status[256]="Ready.";

while(!glfwWindowShouldClose(win)){
glfwPollEvents(); ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame();
ImGui::SetNextWindowPos(ImVec2(0,0), ImGuiCond_Always); ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);
ImGui::Begin("Main", nullptr, ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoCollapse);

ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f,0.72f,1,1)); ImGui::Text("8D AUDIO PROCESSOR v8"); ImGui::PopStyleColor();
ImGui::TextDisabled("Enhanced HRTF Convolution Engine."); ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

// DEVICES
ImGui::SeparatorText("DEVICES");
float cw = (ImGui::GetContentRegionAvail().x - 20) * 0.5f;
ImGui::BeginGroup(); ImGui::TextDisabled("Input"); ImGui::SetNextItemWidth(cw);
if(!inN.empty() && ImGui::BeginCombo("##in", inN[sIn].c_str())){ for(int i=0;i<(int)inN.size();++i) if(ImGui::Selectable(inN[i].c_str(), isIn)) sIn=i; ImGui::EndCombo(); }
ImGui::EndGroup(); ImGui::SameLine(0, 20);
ImGui::BeginGroup(); ImGui::TextDisabled("Output"); ImGui::SetNextItemWidth(cw);
if(!outN.empty() && ImGui::BeginCombo("##out", outN[sOut].c_str())){ for(int i=0;i<(int)outN.size();++i) if(ImGui::Selectable(outN[i].c_str(), isOut)) sOut=i; ImGui::EndCombo(); }
ImGui::EndGroup();

ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

// CONTROLS
ImGui::SeparatorText("CONTROLS");
if(audio.isRunning()){
ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f,0.25f,0.25f,1));
if(ImGui::Button("STOP", ImVec2(160, 40))){ audio.stop(); status="Stopped."; }
ImGui::PopStyleColor();
} else {
ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f,0.55f,0.3f,1));
if(ImGui::Button("START", ImVec2(160, 40))){ audio.start(inI[sIn], outI[sOut]); status="Running."; }
ImGui::PopStyleColor();
}


ImGui::Spacing(); ImGui::TextDisabled("Rotation Mode:");
ImGui::RadioButton("Circle", &mode, 0); ImGui::SameLine(); ImGui::RadioButton("Figure-8", &mode, 1);
audio.setFig8(mode==1);

ImGui::TextDisabled("Speed (Hz)"); ImGui::SetNextItemWidth(-1);
if(ImGui::SliderFloat("##spd", &rotSpd, 0.f, 1.f, "%.2f Hz")) audio.setSpeed(rotSpd);

ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

// EFFECTS
ImGui::SeparatorText("EFFECTS");

if(ImGui::Checkbox("HRTF (Enhanced)", &hrtfOn)) audio.setHrtf(hrtfOn); ImGui::SameLine();
if(ImGui::Checkbox("ITD", &itdOn)) audio.setItd(itdOn); ImGui::SameLine();
if(ImGui::Checkbox("Back Filter", &filtOn)) audio.setFilt(filtOn);

if(ImGui::Checkbox("Reverb", &revOn)) audio.setRev(revOn); ImGui::SameLine();
if(ImGui::Checkbox("Compressor", &compOn)) audio.setComp(compOn); ImGui::SameLine();
if(ImGui::Checkbox("Limiter", &limOn)) audio.setLim(limOn);

if(ImGui::Checkbox("Crossfeed", &cfOn)) audio.setCrossfeed(cfOn); ImGui::SameLine();
ImGui::SetNextItemWidth(100); if(ImGui::SliderFloat("##cf", &cfVal, 0.f, 1.f, "CF %.2f")) audio.cfAmt_.store(cfVal);

if(ImGui::Checkbox("De-esser", &dsOn)) audio.setDeEsser(dsOn); ImGui::SameLine();
ImGui::SetNextItemWidth(100); if(ImGui::SliderFloat("##ds", &dsVal, 0.f, 1.f, "DS %.2f")) audio.dsAmt_.store(dsVal);

ImGui::Spacing();
if(ImGui::Checkbox("Stereo Width", &widOn)) audio.setWidth(widOn);
ImGui::SetNextItemWidth(-1); if(ImGui::SliderFloat("##wid", &widthVal, 0.f, 3.f, "%.2f")) audio.wid_.store(widthVal);

ImGui::Spacing();
ImGui::TextDisabled("Bass Boost"); ImGui::SetNextItemWidth(-1);
if(ImGui::SliderFloat("##bass", &bassDb, 0.f, 12.f, "+%.1f dB")) audio.setBass(bassDb);

ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

// LEVEL
ImGui::SeparatorText("LEVEL");
ImGui::ProgressBar(audio.getLevelL(), ImVec2(-1,0));
ImGui::ProgressBar(audio.getLevelR(), ImVec2(-1,0));

ImGui::Spacing(); ImGui::TextDisabled("%s", status);
ImGui::End();

ImGui::Render(); int w,h; glfwGetFramebufferSize(win,&w,&h); glViewport(0,0,w,h);
glClearColor(0.06f,0.07f,0.09f,1.f); glClear(GL_COLOR_BUFFER_BIT);
ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData()); glfwSwapBuffers(win);
}

audio.stop(); ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplGlfw_Shutdown(); ImGui::DestroyContext(); glfwDestroyWindow(win); glfwTerminate(); return 0;
}
