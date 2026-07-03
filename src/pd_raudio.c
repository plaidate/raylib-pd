/**********************************************************************************************
*
*   pd_raudio - raylib audio API (raudio subset) implemented over the Playdate's pd->sound
*
*   Drop-in replacement for the raudio module on PLATFORM_PLAYDATE: implements the
*   Sound and Music halves of raylib's audio API with the exact raylib.h signatures,
*   so upstream examples compile unchanged. raudio.c itself is not compiled (miniaudio
*   has no Playdate backend); this module maps onto the Playdate sound engine instead:
*
*       Sound  -> AudioSample + SamplePlayer  (whole file in memory; .wav/.pda)
*       Music  -> FilePlayer                  (streamed; .mp3/.pda)
*
*   The Playdate pointers ride inside raylib's own structs: Sound.stream.buffer holds
*   a PdSound wrapper, Music.ctxData holds a PdMusic wrapper.
*
*   Not supported (no-ops with a warning where sensible): audio streams/processors,
*   LoadSoundFromWave/Wave manipulation, LoadMusicStreamFromMemory, attached effects.
*   UpdateMusicStream() is a no-op: the Playdate streams on its own thread.
*
*   LICENSE: zlib/libpng
*
**********************************************************************************************/

#if defined(PLATFORM_PLAYDATE)

#include "raylib.h"

#include "pd_api.h"

#include <math.h>       // sqrtf() [equal-power panning]
#include <stdlib.h>     // RL_MALLOC/RL_FREE default to malloc/free
#include <string.h>     // strrchr()/memcpy() [extension fallbacks, WAV parse]

PlaydateAPI *GetPlaydateAPI(void);      // Provided by the platform backend (rcore_playdate.c)

#ifndef RL_MALLOC
    #define RL_MALLOC(sz) malloc(sz)
#endif
#ifndef RL_FREE
    #define RL_FREE(ptr) free(ptr)
#endif

#ifndef TRACELOG
    #define TRACELOG(level, ...) TraceLog(level, __VA_ARGS__)
#endif

typedef struct PdSound {
    SamplePlayer *player;
    AudioSample *sample;
    float volume;       // 0..1
    float pitch;        // Playback rate multiplier
    float pan;          // 0..1, 0.5 = center (raylib convention)
} PdSound;

typedef struct PdMusic {
    FilePlayer *player;
    float volume;
    float pan;
} PdMusic;

static bool audioDeviceReady = false;
static float masterVolume = 1.0f;

// Equal-power pan: raylib pan is 0..1 with 0.5 center
static void PdApplyVolume(float volume, float pan, float *left, float *right)
{
    *left = volume*sqrtf(1.0f - pan);
    *right = volume*sqrtf(pan);
}

//----------------------------------------------------------------------------------
// Host-side command queue (simulator green-thread builds)
// The simulator sound engine is not safe to MUTATE from the game pthread:
// stopping/starting FilePlayers there deadlocks the whole process (seen
// deterministically in transmission's mission-screen music swap). Player
// mutations are queued and drained on the host update thread by
// PdRaudioHostPump(); loads and reads stay synchronous (they need return
// values and have proven safe). Device + Lua builds run single-context and
// call straight through. Consequence: state reads right after a mutation
// (e.g. IsSoundPlaying() after PlaySound()) can lag up to one host frame.
//----------------------------------------------------------------------------------
#if defined(TARGET_SIMULATOR) && !defined(PD_RAYLIB_LUA)
    #define PD_SND_QUEUE 1
    #include <sched.h>
#endif

typedef enum {
    CMD_SP_PLAY, CMD_SP_STOP, CMD_SP_SETPAUSED, CMD_SP_SETVOL, CMD_SP_SETRATE, CMD_SP_FREE,
    CMD_FP_PLAY, CMD_FP_STOP, CMD_FP_PAUSE, CMD_FP_SETVOL, CMD_FP_SETRATE, CMD_FP_SETOFFSET, CMD_FP_FREE,
    CMD_CH_SETVOL
} SndOp;

typedef struct { SndOp op; void *p; void *p2; float a, b; int i; } SndCmd;

static void SndExec(const SndCmd *c)
{
    PlaydateAPI *pd = GetPlaydateAPI();
    if (pd == NULL) return;
    switch (c->op)
    {
        case CMD_SP_PLAY: pd->sound->sampleplayer->play((SamplePlayer *)c->p, c->i, c->a); break;
        case CMD_SP_STOP: pd->sound->sampleplayer->stop((SamplePlayer *)c->p); break;
        case CMD_SP_SETPAUSED: pd->sound->sampleplayer->setPaused((SamplePlayer *)c->p, c->i); break;
        case CMD_SP_SETVOL: pd->sound->sampleplayer->setVolume((SamplePlayer *)c->p, c->a, c->b); break;
        case CMD_SP_SETRATE: pd->sound->sampleplayer->setRate((SamplePlayer *)c->p, c->a); break;
        case CMD_SP_FREE:
            pd->sound->sampleplayer->stop((SamplePlayer *)c->p);
            pd->sound->sampleplayer->freePlayer((SamplePlayer *)c->p);
            pd->sound->sample->freeSample((AudioSample *)c->p2);
            break;
        case CMD_FP_PLAY: pd->sound->fileplayer->play((FilePlayer *)c->p, c->i); break;
        case CMD_FP_STOP: pd->sound->fileplayer->stop((FilePlayer *)c->p); break;
        case CMD_FP_PAUSE: pd->sound->fileplayer->pause((FilePlayer *)c->p); break;
        case CMD_FP_SETVOL: pd->sound->fileplayer->setVolume((FilePlayer *)c->p, c->a, c->b); break;
        case CMD_FP_SETRATE: pd->sound->fileplayer->setRate((FilePlayer *)c->p, c->a); break;
        case CMD_FP_SETOFFSET: pd->sound->fileplayer->setOffset((FilePlayer *)c->p, c->a); break;
        case CMD_FP_FREE:
            pd->sound->fileplayer->stop((FilePlayer *)c->p);
            pd->sound->fileplayer->freePlayer((FilePlayer *)c->p);
            break;
        case CMD_CH_SETVOL: pd->sound->channel->setVolume(pd->sound->getDefaultChannel(), c->a); break;
        default: break;
    }
}

#if defined(PD_SND_QUEUE)
#define SND_QLEN 128
static SndCmd sndQueue[SND_QLEN];
static volatile unsigned int sndQHead = 0, sndQTail = 0;    // game produces, host consumes

// Drain pending commands; called from the Playdate update callback
// (rcore_playdate.c) after each game slice
void PdRaudioHostPump(void)
{
    while (sndQHead != sndQTail)
    {
        SndCmd c = sndQueue[sndQHead % SND_QLEN];
        __sync_synchronize();
        sndQHead = sndQHead + 1;
        SndExec(&c);
    }
}

static void SndSubmit(SndCmd c)
{
    while ((sndQTail - sndQHead) >= SND_QLEN) sched_yield();    // full: host drains every frame
    sndQueue[sndQTail % SND_QLEN] = c;
    __sync_synchronize();
    sndQTail = sndQTail + 1;
}
#else
static void SndSubmit(SndCmd c) { SndExec(&c); }
#endif

//----------------------------------------------------------------------------------
// Audio device
//----------------------------------------------------------------------------------
void InitAudioDevice(void)
{
    audioDeviceReady = (GetPlaydateAPI() != NULL);
    if (audioDeviceReady) TRACELOG(LOG_INFO, "AUDIO: Playdate sound engine ready (pd->sound)");
    else TRACELOG(LOG_WARNING, "AUDIO: PlaydateAPI not available");
}

void CloseAudioDevice(void) { audioDeviceReady = false; }
bool IsAudioDeviceReady(void) { return audioDeviceReady; }

void SetMasterVolume(float volume)
{
    masterVolume = volume;
    if (GetPlaydateAPI() != NULL) SndSubmit((SndCmd){ .op = CMD_CH_SETVOL, .a = volume });
}

float GetMasterVolume(void) { return masterVolume; }

//----------------------------------------------------------------------------------
// Sound (AudioSample + SamplePlayer)
//----------------------------------------------------------------------------------
Sound LoadSound(const char *fileName)
{
    Sound sound = { 0 };
    PlaydateAPI *pd = GetPlaydateAPI();
    if (pd == NULL) return sound;

    // pdc compiles .wav sources into .pda inside the pdx, and pd->sound->sample->load
    // resolves extension-less paths against them — so strip the extension first,
    // letting raylib-style paths ("resources/coin.wav") find "resources/coin.pda"
    char stripped[256] = { 0 };
    const char *dot = NULL;
    for (const char *p = fileName; *p != '\0'; p++) if (*p == '.') dot = p;
    size_t len = (dot != NULL)? (size_t)(dot - fileName) : sizeof(stripped) - 1;
    if (len >= sizeof(stripped)) len = sizeof(stripped) - 1;
    for (size_t i = 0; i < len && fileName[i] != '\0'; i++) stripped[i] = fileName[i];

    AudioSample *sample = pd->sound->sample->load(stripped);
    if (sample == NULL) sample = pd->sound->sample->load(fileName);    // Fall back to the literal path
    if (sample == NULL)
    {
        TRACELOG(LOG_WARNING, "AUDIO: [%s] Failed to load sample (WAV PCM/ADPCM or .pda)", fileName);
        return sound;
    }

    PdSound *pds = (PdSound *)RL_MALLOC(sizeof(PdSound));
    pds->sample = sample;
    pds->player = pd->sound->sampleplayer->newPlayer();
    pds->volume = 1.0f;
    pds->pitch = 1.0f;
    pds->pan = 0.5f;
    pd->sound->sampleplayer->setSample(pds->player, sample);

    sound.stream.buffer = (rAudioBuffer *)pds;
    sound.stream.sampleRate = 44100;
    sound.stream.sampleSize = 16;
    sound.stream.channels = 1;
    sound.frameCount = (unsigned int)(pd->sound->sample->getLength(sample)*44100.0f);

    TRACELOG(LOG_INFO, "AUDIO: [%s] Sound loaded (%.2fs)", fileName, pd->sound->sample->getLength(sample));
    return sound;
}

bool IsSoundValid(Sound sound) { return (sound.stream.buffer != NULL); }

void UnloadSound(Sound sound)
{
    PdSound *pds = (PdSound *)sound.stream.buffer;
    PlaydateAPI *pd = GetPlaydateAPI();
    if ((pds == NULL) || (pd == NULL)) return;

    SndSubmit((SndCmd){ .op = CMD_SP_FREE, .p = pds->player, .p2 = pds->sample });
    RL_FREE(pds);
}

void PlaySound(Sound sound)
{
    PdSound *pds = (PdSound *)sound.stream.buffer;
    PlaydateAPI *pd = GetPlaydateAPI();
    if ((pds == NULL) || (pd == NULL)) return;

    float l, r;
    PdApplyVolume(pds->volume, pds->pan, &l, &r);
    SndSubmit((SndCmd){ .op = CMD_SP_SETVOL, .p = pds->player, .a = l, .b = r });
    SndSubmit((SndCmd){ .op = CMD_SP_PLAY, .p = pds->player, .i = 1, .a = pds->pitch });
}

void StopSound(Sound sound)
{
    PdSound *pds = (PdSound *)sound.stream.buffer;
    if ((pds != NULL) && (GetPlaydateAPI() != NULL)) SndSubmit((SndCmd){ .op = CMD_SP_STOP, .p = pds->player });
}

void PauseSound(Sound sound)
{
    PdSound *pds = (PdSound *)sound.stream.buffer;
    if ((pds != NULL) && (GetPlaydateAPI() != NULL)) SndSubmit((SndCmd){ .op = CMD_SP_SETPAUSED, .p = pds->player, .i = 1 });
}

void ResumeSound(Sound sound)
{
    PdSound *pds = (PdSound *)sound.stream.buffer;
    if ((pds != NULL) && (GetPlaydateAPI() != NULL)) SndSubmit((SndCmd){ .op = CMD_SP_SETPAUSED, .p = pds->player, .i = 0 });
}

bool IsSoundPlaying(Sound sound)
{
    PdSound *pds = (PdSound *)sound.stream.buffer;
    if ((pds == NULL) || (GetPlaydateAPI() == NULL)) return false;
    return (GetPlaydateAPI()->sound->sampleplayer->isPlaying(pds->player) != 0);
}

void SetSoundVolume(Sound sound, float volume)
{
    PdSound *pds = (PdSound *)sound.stream.buffer;
    PlaydateAPI *pd = GetPlaydateAPI();
    if ((pds == NULL) || (pd == NULL)) return;
    pds->volume = volume;
    float l, r;
    PdApplyVolume(pds->volume, pds->pan, &l, &r);
    SndSubmit((SndCmd){ .op = CMD_SP_SETVOL, .p = pds->player, .a = l, .b = r });
}

void SetSoundPitch(Sound sound, float pitch)
{
    PdSound *pds = (PdSound *)sound.stream.buffer;
    if ((pds == NULL) || (GetPlaydateAPI() == NULL)) return;
    pds->pitch = pitch;
    SndSubmit((SndCmd){ .op = CMD_SP_SETRATE, .p = pds->player, .a = pitch });
}

void SetSoundPan(Sound sound, float pan)
{
    PdSound *pds = (PdSound *)sound.stream.buffer;
    PlaydateAPI *pd = GetPlaydateAPI();
    if ((pds == NULL) || (pd == NULL)) return;
    pds->pan = pan;
    float l, r;
    PdApplyVolume(pds->volume, pds->pan, &l, &r);
    SndSubmit((SndCmd){ .op = CMD_SP_SETVOL, .p = pds->player, .a = l, .b = r });
}

//----------------------------------------------------------------------------------
// Music (FilePlayer, streamed)
//----------------------------------------------------------------------------------
//----------------------------------------------------------------------------------
// Wave (CPU-side sample access; PCM RIFF WAV only)
// The Playdate has no ogg decoder, so staging converts waveform-analysis .ogg
// files to raw .wav (see raylib-games-pd stage_resources.sh) and LoadWave()
// falls back from "name.ogg" to "name.wav"
//----------------------------------------------------------------------------------
Wave LoadWave(const char *fileName)
{
    Wave wave = { 0 };

    int dataSize = 0;
    unsigned char *fileData = LoadFileData(fileName, &dataSize);
    if (fileData == NULL)
    {
        char altName[256] = { 0 };
        const char *dot = strrchr(fileName, '.');
        size_t len = (dot != NULL)? (size_t)(dot - fileName) : strlen(fileName);
        if (len > sizeof(altName) - 5) len = sizeof(altName) - 5;
        memcpy(altName, fileName, len);
        memcpy(altName + len, ".wav", 5);
        fileData = LoadFileData(altName, &dataSize);
    }
    if ((fileData == NULL) || (dataSize < 44))
    {
        TRACELOG(LOG_WARNING, "AUDIO: [%s] Failed to load wave (PCM .wav)", fileName);
        if (fileData != NULL) UnloadFileData(fileData);
        return wave;
    }

    if ((memcmp(fileData, "RIFF", 4) != 0) || (memcmp(fileData + 8, "WAVE", 4) != 0))
    {
        TRACELOG(LOG_WARNING, "AUDIO: [%s] Not a RIFF WAVE file", fileName);
        UnloadFileData(fileData);
        return wave;
    }

    // Walk the chunks for "fmt " and "data"
    unsigned int pos = 12;
    unsigned short audioFormat = 0, channels = 0, bitsPerSample = 0;
    unsigned int sampleRate = 0;
    const unsigned char *pcm = NULL;
    unsigned int pcmSize = 0;
    while (pos + 8 <= (unsigned int)dataSize)
    {
        unsigned int chunkSize = 0;
        memcpy(&chunkSize, fileData + pos + 4, 4);
        if (memcmp(fileData + pos, "fmt ", 4) == 0)
        {
            memcpy(&audioFormat, fileData + pos + 8, 2);
            memcpy(&channels, fileData + pos + 10, 2);
            memcpy(&sampleRate, fileData + pos + 12, 4);
            memcpy(&bitsPerSample, fileData + pos + 22, 2);
        }
        else if (memcmp(fileData + pos, "data", 4) == 0)
        {
            pcm = fileData + pos + 8;
            pcmSize = chunkSize;
            if (pos + 8 + pcmSize > (unsigned int)dataSize) pcmSize = (unsigned int)dataSize - pos - 8;
        }
        pos += 8 + chunkSize + (chunkSize & 1);
    }

    if ((audioFormat != 1) || (pcm == NULL) || (channels == 0) ||
        ((bitsPerSample != 8) && (bitsPerSample != 16)))
    {
        TRACELOG(LOG_WARNING, "AUDIO: [%s] Unsupported WAV layout (need PCM 8/16-bit)", fileName);
        UnloadFileData(fileData);
        return wave;
    }

    wave.frameCount = pcmSize/(channels*(bitsPerSample/8));
    wave.sampleRate = sampleRate;
    wave.sampleSize = bitsPerSample;
    wave.channels = channels;
    wave.data = RL_MALLOC(pcmSize);
    memcpy(wave.data, pcm, pcmSize);
    UnloadFileData(fileData);

    TRACELOG(LOG_INFO, "AUDIO: [%s] Wave loaded (%u frames, %u Hz, %u ch, %u bit)",
             fileName, wave.frameCount, wave.sampleRate, wave.channels, wave.sampleSize);
    return wave;
}

void UnloadWave(Wave wave)
{
    if (wave.data != NULL) RL_FREE(wave.data);
}

float *LoadWaveSamples(Wave wave)
{
    if ((wave.data == NULL) || (wave.frameCount == 0)) return NULL;

    unsigned int count = wave.frameCount*wave.channels;
    float *samples = (float *)RL_MALLOC(count*sizeof(float));
    if (wave.sampleSize == 16)
    {
        const short *src = (const short *)wave.data;
        for (unsigned int i = 0; i < count; i++) samples[i] = (float)src[i]/32768.0f;
    }
    else // 8-bit unsigned
    {
        const unsigned char *src = (const unsigned char *)wave.data;
        for (unsigned int i = 0; i < count; i++) samples[i] = ((float)src[i] - 128.0f)/128.0f;
    }
    return samples;
}

void UnloadWaveSamples(float *samples)
{
    if (samples != NULL) RL_FREE(samples);
}

Music LoadMusicStream(const char *fileName)
{
    Music music = { 0 };
    PlaydateAPI *pd = GetPlaydateAPI();
    if (pd == NULL) return music;

    FilePlayer *player = pd->sound->fileplayer->newPlayer();
    int loaded = pd->sound->fileplayer->loadIntoPlayer(player, fileName);
    if (loaded == 0)
    {
        // Formats the Playdate can't stream (e.g. raylib games shipping .ogg)
        // are converted at staging time: retry as .mp3, then extension-less
        // (which resolves against a pdc-compiled .pda)
        char altName[256] = { 0 };
        const char *dot = strrchr(fileName, '.');
        size_t len = (dot != NULL)? (size_t)(dot - fileName) : strlen(fileName);
        if (len > sizeof(altName) - 5) len = sizeof(altName) - 5;
        memcpy(altName, fileName, len);

        memcpy(altName + len, ".mp3", 5);
        loaded = pd->sound->fileplayer->loadIntoPlayer(player, altName);

        if (loaded == 0)
        {
            altName[len] = '\0';
            loaded = pd->sound->fileplayer->loadIntoPlayer(player, altName);
        }
    }
    if (loaded == 0)
    {
        TRACELOG(LOG_WARNING, "AUDIO: [%s] Failed to open music stream (.mp3 or .pda)", fileName);
        pd->sound->fileplayer->freePlayer(player);
        return music;
    }

    PdMusic *pdm = (PdMusic *)RL_MALLOC(sizeof(PdMusic));
    pdm->player = player;
    pdm->volume = 1.0f;
    pdm->pan = 0.5f;

    music.ctxData = pdm;
    music.looping = true;       // raylib default
    music.stream.sampleRate = 44100;
    music.stream.sampleSize = 16;
    music.stream.channels = 2;
    music.frameCount = (unsigned int)(pd->sound->fileplayer->getLength(player)*44100.0f);

    TRACELOG(LOG_INFO, "AUDIO: [%s] Music stream opened (%.2fs)", fileName, pd->sound->fileplayer->getLength(player));
    return music;
}

bool IsMusicValid(Music music) { return (music.ctxData != NULL); }

void UnloadMusicStream(Music music)
{
    PdMusic *pdm = (PdMusic *)music.ctxData;
    PlaydateAPI *pd = GetPlaydateAPI();
    if ((pdm == NULL) || (pd == NULL)) return;
    SndSubmit((SndCmd){ .op = CMD_FP_FREE, .p = pdm->player });
    RL_FREE(pdm);
}

void PlayMusicStream(Music music)
{
    PdMusic *pdm = (PdMusic *)music.ctxData;
    PlaydateAPI *pd = GetPlaydateAPI();
    if ((pdm == NULL) || (pd == NULL)) return;

    float l, r;
    PdApplyVolume(pdm->volume, pdm->pan, &l, &r);
    SndSubmit((SndCmd){ .op = CMD_FP_SETVOL, .p = pdm->player, .a = l, .b = r });
    SndSubmit((SndCmd){ .op = CMD_FP_PLAY, .p = pdm->player, .i = music.looping? 0 : 1 });  // 0 = loop forever
}

bool IsMusicStreamPlaying(Music music)
{
    PdMusic *pdm = (PdMusic *)music.ctxData;
    if ((pdm == NULL) || (GetPlaydateAPI() == NULL)) return false;
    return (GetPlaydateAPI()->sound->fileplayer->isPlaying(pdm->player) != 0);
}

// The Playdate streams and mixes on its own; nothing to pump per-frame
void UpdateMusicStream(Music music) { (void)music; }

void StopMusicStream(Music music)
{
    PdMusic *pdm = (PdMusic *)music.ctxData;
    if ((pdm != NULL) && (GetPlaydateAPI() != NULL)) SndSubmit((SndCmd){ .op = CMD_FP_STOP, .p = pdm->player });
}

void PauseMusicStream(Music music)
{
    PdMusic *pdm = (PdMusic *)music.ctxData;
    if ((pdm != NULL) && (GetPlaydateAPI() != NULL)) SndSubmit((SndCmd){ .op = CMD_FP_PAUSE, .p = pdm->player });
}

void ResumeMusicStream(Music music)
{
    PdMusic *pdm = (PdMusic *)music.ctxData;
    if ((pdm != NULL) && (GetPlaydateAPI() != NULL)) SndSubmit((SndCmd){ .op = CMD_FP_PLAY, .p = pdm->player, .i = music.looping? 0 : 1 });
}

void SeekMusicStream(Music music, float position)
{
    PdMusic *pdm = (PdMusic *)music.ctxData;
    if ((pdm != NULL) && (GetPlaydateAPI() != NULL)) SndSubmit((SndCmd){ .op = CMD_FP_SETOFFSET, .p = pdm->player, .a = position });
}

void SetMusicVolume(Music music, float volume)
{
    PdMusic *pdm = (PdMusic *)music.ctxData;
    PlaydateAPI *pd = GetPlaydateAPI();
    if ((pdm == NULL) || (pd == NULL)) return;
    pdm->volume = volume;
    float l, r;
    PdApplyVolume(pdm->volume, pdm->pan, &l, &r);
    SndSubmit((SndCmd){ .op = CMD_FP_SETVOL, .p = pdm->player, .a = l, .b = r });
}

void SetMusicPitch(Music music, float pitch)
{
    PdMusic *pdm = (PdMusic *)music.ctxData;
    if ((pdm != NULL) && (GetPlaydateAPI() != NULL)) SndSubmit((SndCmd){ .op = CMD_FP_SETRATE, .p = pdm->player, .a = pitch });
}

void SetMusicPan(Music music, float pan)
{
    PdMusic *pdm = (PdMusic *)music.ctxData;
    PlaydateAPI *pd = GetPlaydateAPI();
    if ((pdm == NULL) || (pd == NULL)) return;
    pdm->pan = pan;
    float l, r;
    PdApplyVolume(pdm->volume, pdm->pan, &l, &r);
    SndSubmit((SndCmd){ .op = CMD_FP_SETVOL, .p = pdm->player, .a = l, .b = r });
}

float GetMusicTimeLength(Music music)
{
    PdMusic *pdm = (PdMusic *)music.ctxData;
    if ((pdm == NULL) || (GetPlaydateAPI() == NULL)) return 0.0f;
    return GetPlaydateAPI()->sound->fileplayer->getLength(pdm->player);
}

float GetMusicTimePlayed(Music music)
{
    PdMusic *pdm = (PdMusic *)music.ctxData;
    if ((pdm == NULL) || (GetPlaydateAPI() == NULL)) return 0.0f;
    return GetPlaydateAPI()->sound->fileplayer->getOffset(pdm->player);
}

#endif // PLATFORM_PLAYDATE
