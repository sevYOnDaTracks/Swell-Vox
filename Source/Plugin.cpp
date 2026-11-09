#include <juce_audio_utils/juce_audio_utils.h>
#include "Licence/SwellLicence.h"
#include <juce_dsp/juce_dsp.h>
#include "BinaryData.h"
#include "VoxDSP.h"
class LiftProcessor;
namespace {
const juce::Colour background{0xff1b1c20}, screen{0xff121316}, border{0xff34353b};
const juce::Colour text{0xffeeeef1}, dim{0xffa4a5af}, accent{0xffaebeff}, original{0xff8891a3};
void label(juce::Graphics& g,const juce::String& s,juce::Rectangle<float> r,float size,juce::Colour colour,juce::Justification align=juce::Justification::left,bool bold=false)
{g.setColour(colour);g.setFont(juce::FontOptions(size,bold?juce::Font::bold:juce::Font::plain));g.drawText(s,r,align);}
}
class LiftLook : public juce::LookAndFeel_V4 { public:
 LiftLook(){setColour(juce::TextButton::buttonColourId,juce::Colour(0xff25262b));setColour(juce::TextButton::buttonOnColourId,juce::Colour(0xff35394f));setColour(juce::TextButton::textColourOnId,accent);setColour(juce::ComboBox::backgroundColourId,juce::Colour(0xff25262b));setColour(juce::ComboBox::outlineColourId,border);}
 void drawRotarySlider(juce::Graphics& g,int x,int y,int width,int height,float pos,float start,float end,juce::Slider& slider)
{
    auto r=juce::Rectangle<float>((float)x,(float)y,(float)width,(float)height).reduced(8);
    float size=juce::jmin(r.getWidth(),r.getHeight());r=r.withSizeKeepingCentre(size,size);
    auto centre=r.getCentre();float radius=size*.5f;bool main=slider.getName()=="Compress";
    auto colour=main?accent:juce::Colour(0xffafb4c1);
    juce::Path track,arc;track.addCentredArc(centre.x,centre.y,radius-4,radius-4,0,start,end,true);
    arc.addCentredArc(centre.x,centre.y,radius-4,radius-4,0,start,start+pos*(end-start),true);
    g.setColour(border);g.strokePath(track,juce::PathStrokeType(5));g.setColour(colour);g.strokePath(arc,juce::PathStrokeType(5));
    auto face=r.reduced(main?23.f:18.f);g.setGradientFill(juce::ColourGradient(juce::Colour(0xff36373e),face.getTopLeft(),background,face.getBottomRight(),false));
    g.fillEllipse(face);g.setColour(juce::Colour(0xff494b53));g.drawEllipse(face,1);
    float angle=start+pos*(end-start);auto p=centre.getPointOnCircumference(radius-(main?32.f:26.f),angle);
    auto q=centre.getPointOnCircumference(radius-(main?51.f:40.f),angle);g.setColour(colour);g.drawLine({p,q},main?4.f:3.f);
    bool db=slider.getName()=="Output";auto value=db?juce::String((slider.getValue()-50)*.24,1):juce::String((int)std::round(slider.getValue()));
    if(db&&slider.getValue()>50)value="+"+value;
    label(g,value,{centre.x-55,centre.y-16,110,30},main?32.f:21.f,text,juce::Justification::centred,true);
    label(g,db?"dB":main?"%":"%",{centre.x-55,centre.y+15,110,16},10,dim,juce::Justification::centred);
}
};
class LiftEditor : public juce::AudioProcessorEditor,private juce::Timer {
 LiftProcessor& proc;LiftLook look;juce::Slider knob; juce::Slider extra[5]; std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachments[5];juce::TextButton bypass{"Bypass"};juce::Image logo;
 std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> ka;
 std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> ba;
 juce::ComboBox presets;juce::TextButton reset{"Retablir"},slotA{"A"},slotB{"B"},copy{"A > B"};
 float slots[2][6]={{35,25,25,25,25,50},{35,25,25,25,25,50}};
 float scale=1.f; int active=0,head=0;float history[2][180]{},levels[2]{};
 void setValues(const float*);void saveSlot();void timerCallback() override;
public:
 explicit LiftEditor(LiftProcessor&);
 ~LiftEditor() override {setLookAndFeel(nullptr);knob.setLookAndFeel(nullptr);for(auto& s:extra)s.setLookAndFeel(nullptr);}
 void paint(juce::Graphics&) override;
 void resized() override {
 scale=getWidth()/1000.f;
 auto place=[this](juce::Component& c,int x,int y,int w,int h){
 c.setTransform(juce::AffineTransform());c.setBounds(x,y,w,h);
 c.setTransform(juce::AffineTransform::scale(scale));};

 place(knob,38,443,140,140);
 place(extra[0],344,416,194,194);
 place(extra[1],192,443,140,140);
 place(extra[2],550,443,140,140);
 place(extra[3],704,443,140,140);
 place(extra[4],850,443,125,140);
 place(presets,390,24,240,35);place(slotA,820,24,34,35);place(slotB,861,24,34,35);place(copy,905,24,67,35);
 place(reset,484,676,130,32);place(bypass,630,676,90,32);
 }
};
class LiftProcessor : public juce::AudioProcessor {
public:
    // Licence Swell : partagée par toutes les instances ; silence tant que le plugin n'est pas activé.
    juce::SharedResourcePointer<swell::Licence> licence;

public:
 juce::AudioProcessorValueTreeState state;
 juce::SmoothedValue<float> amount,enabled; juce::SmoothedValue<float> controls[5];
 VoxDSP dsp; std::atomic<float> inputMeter{0},outputMeter{0};
 std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
 juce::dsp::DryWetMixer<float> mixer{64};
 LiftProcessor():AudioProcessor(BusesProperties().withInput("Input",juce::AudioChannelSet::stereo(),true).withOutput("Output",juce::AudioChannelSet::stereo(),true)),state(*this,nullptr,"Vox",layout()){}
 static juce::AudioProcessorValueTreeState::ParameterLayout layout(){juce::AudioProcessorValueTreeState::ParameterLayout l;l.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"lift",1},"Clean",juce::NormalisableRange<float>(0,100,.1f),35.f));l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"bypass",1},"Bypass",false));for(auto id:{"compress","deess","air","warmth","output"})l.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{id,1},id,juce::NormalisableRange<float>(0,100,.1f),juce::String(id)=="output"?50.f:25.f));return l;}
 const juce::String getName() const override{return "Swell VOX";}
 bool acceptsMidi()const override{return false;}bool producesMidi()const override{return false;}double getTailLengthSeconds()const override{return 0;}
 bool isBusesLayoutSupported(const BusesLayout& l)const override{return (l.getMainOutputChannelSet()==juce::AudioChannelSet::mono()||l.getMainOutputChannelSet()==juce::AudioChannelSet::stereo())&&l.getMainOutputChannelSet()==l.getMainInputChannelSet();}
 void prepareToPlay(double sr,int block) override {
  auto channels=(size_t)getTotalNumInputChannels();oversampling=std::make_unique<juce::dsp::Oversampling<float>>(channels,2,juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,true,true);oversampling->initProcessing((size_t)block);
  auto latency=oversampling->getLatencyInSamples();setLatencySamples((int)latency);mixer.prepare({sr,(juce::uint32)block,(juce::uint32)channels});mixer.setWetLatency(latency);mixer.setMixingRule(juce::dsp::DryWetMixingRule::linear);mixer.reset();
  dsp.prepare(sr*4);int k=0;for(auto id:{"compress","deess","air","warmth","output"}){controls[k].reset(sr*4,.03);controls[k++].setCurrentAndTargetValue(state.getRawParameterValue(id)->load()/100);}amount.reset(sr*4,.03);amount.setCurrentAndTargetValue(state.getRawParameterValue("lift")->load()/100);enabled.reset(sr,.015);enabled.setCurrentAndTargetValue(state.getRawParameterValue("bypass")->load()>.5f?0.f:1.f);
 }
 void releaseResources() override{}
 void render(juce::AudioBuffer<float>& b,bool hostBypass) {
  juce::ScopedNoDenormals noDenormals;if(b.getNumSamples()==0)return;
  float ip=0;for(int c=0;c<b.getNumChannels();++c)ip=std::max(ip,b.getMagnitude(c,0,b.getNumSamples()));inputMeter.store(ip);juce::dsp::AudioBlock<float> block(b);mixer.pushDrySamples(block);amount.setTargetValue(state.getRawParameterValue("lift")->load()/100);
  // Mixer smooths bypass transitions and aligns dry audio with oversampling latency.
  mixer.setWetMixProportion(hostBypass||state.getRawParameterValue("bypass")->load()>.5f?0.f:1.f);
  int k=0;for(auto id:{"compress","deess","air","warmth","output"})controls[k++].setTargetValue(state.getRawParameterValue(id)->load()/100);auto up=oversampling->processSamplesUp(block);for(size_t i=0;i<up.getNumSamples();++i){float frame[2]{};for(size_t c=0;c<up.getNumChannels();++c)frame[c]=up.getSample((int)c,(int)i);float v[5];for(int k=0;k<5;++k)v[k]=controls[k].getNextValue();dsp.tick(frame,(int)up.getNumChannels(),amount.getNextValue(),v);for(size_t c=0;c<up.getNumChannels();++c)up.setSample((int)c,(int)i,frame[c]);}
  oversampling->processSamplesDown(block);mixer.mixWetSamples(block);float op=0;for(int c=0;c<b.getNumChannels();++c)op=std::max(op,b.getMagnitude(c,0,b.getNumSamples()));outputMeter.store(op);
 }
 juce::AudioProcessorParameter* getBypassParameter() const override{return state.getParameter("bypass");}
 void processBlock(juce::AudioBuffer<float>& b,juce::MidiBuffer&) override{ if (! licence->isUnlocked()) { b.clear(); return; } render(b,false);}
 void processBlockBypassed(juce::AudioBuffer<float>& b,juce::MidiBuffer&) override{render(b,true);}
 bool hasEditor()const override{return true;}juce::AudioProcessorEditor* createEditor()override{return swell::LicenceGuard::attach (new LiftEditor(*this));}
 int getNumPrograms()override{return 1;}int getCurrentProgram()override{return 0;}void setCurrentProgram(int)override{}const juce::String getProgramName(int)override{return "Default";}void changeProgramName(int,const juce::String&)override{}
 void getStateInformation(juce::MemoryBlock& out)override{auto xml=state.copyState().createXml();copyXmlToBinary(*xml,out);}
 void setStateInformation(const void* data,int size)override{auto xml=getXmlFromBinary(data,size);if(xml&&xml->hasTagName(state.state.getType()))state.replaceState(juce::ValueTree::fromXml(*xml));}
};
LiftEditor::LiftEditor(LiftProcessor& p):AudioProcessorEditor(p),proc(p){setLookAndFeel(&look);setResizable(true,true);setResizeLimits(800,584,1500,1095);if(auto* c=getConstrainer())c->setFixedAspectRatio(1000./730.);setSize(1000,730);int k=0;for(auto id:{"compress","deess","air","warmth","output"}){auto& s=extra[k];s.setSliderStyle(juce::Slider::RotaryVerticalDrag);s.setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);s.setLookAndFeel(&look);s.setName(juce::String(id)=="compress"?"Compress":juce::String(id)=="output"?"Output":id);s.setRotaryParameters(juce::MathConstants<float>::pi*1.25f,juce::MathConstants<float>::pi*2.75f,true);s.setDoubleClickReturnValue(true,juce::String(id)=="output"?50:25);addAndMakeVisible(s);attachments[k]=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.state,id,s);++k;}logo=juce::ImageCache::getFromMemory(BinaryData::swellmark_png,BinaryData::swellmark_pngSize);knob.setSliderStyle(juce::Slider::RotaryVerticalDrag);knob.setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);knob.setRotaryParameters(juce::MathConstants<float>::pi*1.25f,juce::MathConstants<float>::pi*2.75f,true);knob.setDoubleClickReturnValue(true,0);knob.setLookAndFeel(&look);knob.setName("Lift intensity");addAndMakeVisible(knob);bypass.setClickingTogglesState(true);bypass.setColour(juce::TextButton::buttonColourId,juce::Colour(0xff25262b));bypass.setColour(juce::TextButton::buttonOnColourId,juce::Colour(0xffaebeff));addAndMakeVisible(bypass);ka=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.state,"lift",knob);ba=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.state,"bypass",bypass);presets.addItem("Natural Voice",1);presets.addItem("Upfront Lead",2);presets.addItem("Soft & Warm",3);presets.setSelectedId(1,juce::dontSendNotification);
 presets.onChange=[this]{const float values[3][6]={{35,25,25,25,25,50},{45,60,50,35,30,45},{25,40,35,10,55,50}};setValues(values[presets.getSelectedId()-1]);};
 reset.onClick=[this]{presets.onChange();};
 slotA.onClick=[this]{saveSlot();active=0;setValues(slots[0]);};slotB.onClick=[this]{saveSlot();active=1;setValues(slots[1]);};
 copy.onClick=[this]{saveSlot();for(int i=0;i<6;++i)slots[1-active][i]=slots[active][i];};
 for(auto* c:{(juce::Component*)&presets,(juce::Component*)&reset,(juce::Component*)&slotA,(juce::Component*)&slotB,(juce::Component*)&copy})addAndMakeVisible(c);
 resized();startTimerHz(30);}

void LiftEditor::setValues(const float* v){int i=0;for(auto id:{"lift","compress","deess","air","warmth","output"}){auto* p=proc.state.getParameter(id);p->beginChangeGesture();p->setValueNotifyingHost(v[i++]/100);p->endChangeGesture();}}
void LiftEditor::saveSlot(){int i=0;for(auto id:{"lift","compress","deess","air","warmth","output"})slots[active][i++]=proc.state.getRawParameterValue(id)->load();}
void LiftEditor::timerCallback(){
 float v[]={proc.inputMeter.load(),proc.outputMeter.load()};
 for(int c=0;c<2;++c){levels[c]=std::max(v[c],levels[c]*.9f);history[c][head]=v[c];}head=(head+1)%180;
 slotA.setToggleState(active==0,juce::dontSendNotification);slotB.setToggleState(active==1,juce::dontSendNotification);copy.setButtonText(active==0?"A > B":"B > A");repaint();
}
void LiftEditor::paint(juce::Graphics& g){
 g.fillAll(background);g.addTransform(juce::AffineTransform::scale(scale));g.setColour(border);g.drawHorizontalLine(84,0,1000);g.drawHorizontalLine(659,0,1000);
 g.drawImageWithin(logo,28,26,32,29,juce::RectanglePlacement::centred);
 label(g,"swell",{73,18,90,35},25,text,juce::Justification::left,true);label(g,"A U D I O",{75,50,90,15},8,dim);
 g.setColour(border);g.drawVerticalLine(174,25,58);label(g,"VOX",{194,22,100,35},24,text,juce::Justification::left,true);
 label(g,"Une voix propre, stable et presente.",{27,99,740,25},12,dim);
 label(g,"VOCAL CHAIN",{815,99,157,25},10,accent,juce::Justification::right);
 g.setColour(screen);g.fillRoundedRectangle({27.f,140.f,946.f,245.f},7);g.setColour(border);g.drawRoundedRectangle({27.f,140.f,946.f,245.f},7,1);g.drawVerticalLine(870,140,385);
 label(g,"Niveau avant / apres",{45,153,300,22},12,text,juce::Justification::left,true);
 label(g,"Avant",{690,153,60,22},10,original);label(g,"Apres",{780,153,60,22},10,accent);
 for(int i=1;i<9;++i){g.setColour(juce::Colour(0xff25262b));g.drawVerticalLine(45+i*90,190,330);}
 for(int i=0;i<4;++i){g.setColour(juce::Colour(0xff25262b));g.drawHorizontalLine(190+i*46,45,850);}
 for(int c=0;c<2;++c){juce::Path path;for(int i=0;i<180;++i){float x=45+i*805.f/179;float y=330-juce::jlimit(0.f,1.f,history[c][(head+i)%180])*140;if(i==0)path.startNewSubPath(x,y);else path.lineTo(x,y);}g.setColour(c?accent:original);g.strokePath(path,juce::PathStrokeType(1.5f));}
 label(g,bypass.getToggleState()?"Bypass / signal original":"Enveloppes de crete / historique en temps reel",{45,344,790,24},10,dim);
 label(g,"Niveaux crete",{875,158,100,18},10,dim,juce::Justification::centred);
 for(int c=0;c<2;++c){float db=juce::Decibels::gainToDecibels(levels[c],-60.f);int x=898+c*39;label(g,c?"OUT":"IN",{(float)x-12,185,35,17},9,dim,juce::Justification::centred);
 for(int i=0;i<20;++i){g.setColour(db>=-48+i*2.4f?(i>18?juce::Colour(0xfff18177):accent):border);g.fillRect(x,331-i*6,9,4);}
 label(g,db<=-60?"-inf":juce::String(db,1),{(float)x-15,339,40,18},9,dim,juce::Justification::centred);}
 label(g,levels[1]>=1?"CLIP SORTIE":"Clip",{875,362,100,17},9,levels[1]>=1?juce::Colour(0xffff9385):dim,juce::Justification::centred);
 const char* names[]={"Clean","De-ess","Air","Warmth","Output"};float xx[]={38,192,550,704,850};
 for(int i=0;i<5;++i)label(g,names[i],{xx[i],421,i==4?125.f:140.f,22},12,text,juce::Justification::centred,true);
 label(g,"COMPRESS",{344,602,194,32},19,text,juce::Justification::centred,true);
 label(g,"FET + OPTO",{344,635,194,18},10,dim,juce::Justification::centred);
 const char* hints[]={"Graves nets","Sifflantes douces","Clarte","Couleur","-12 / +12 dB"};
 for(int i=0;i<5;++i)label(g,hints[i],{xx[i],599,i==4?125.f:140.f,20},10,dim,juce::Justification::centred);
 label(g,"v" JucePlugin_VersionString "  /  by @sevyondatracks",{740,676,232,33},12,dim,juce::Justification::right);
 label(g,"Nettoyer / Stabiliser / Adoucir / Colorer",{27,676,435,33},11,accent);
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new LiftProcessor();}
