/* CloudMoon WebRTC Opus bridge: standard 48 kHz stereo, one coupled stream. */
int ps3audio_cloudmoon_start(void){OPUS_MULTISTREAM_CONFIGURATION cfg;memset(&cfg,0,sizeof(cfg));cfg.sampleRate=48000;cfg.channelCount=2;cfg.streams=1;cfg.coupledStreams=1;cfg.samplesPerFrame=960;cfg.mapping[0]=0;cfg.mapping[1]=1;if(audio_system_initialized)return 0;return ps3_renderer_init(AUDIO_CONFIGURATION_STEREO,&cfg,NULL,0);}
void ps3audio_submit_opus(const uint8_t*data,size_t length){if(!audio_system_initialized||!data||!length||length>0x7fffffffU)return;ps3_renderer_decode_and_play_sample((char*)data,(int)length);}
void ps3audio_cloudmoon_stop(void){ps3_renderer_cleanup();}
