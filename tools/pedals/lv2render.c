/*
    A tiny LV2 host that plays a mono WAV through an LV2 effect and writes
    one of its outputs, for the pedal null tests' reference renders (see
    render_references.sh). Linux only, and only as much LV2 as the plugins
    we compare against need: no UI, no state.

    lv2render PLUGIN.so IN.wav OUT.wav PREROLL ARG...

    ARG is one of:
      in=PORT        the audio input the WAV is played into
      out=PORT       the audio output written to OUT.wav
      silent=PORT    another audio input, given silence (a stereo plugin's right)
      unused=PORT    another audio output, ignored
      atom=PORT      an atom (event) input, given no events
      PORT=VALUE     a control port's value (every control port needs one,
                     output ones too)

    PREROLL samples of silence are played first and left out of OUT.wav, so
    the plugin's start-up (a fade-in, its knobs gliding to their settings)
    is over before the guitar starts. Both WAVs are mono 32-bit float.

    The host offers the features JUCE-made plugins insist on: urid:map,
    options (the block length) and bounded block lengths.

    Build: cc -O2 -I<lv2>/include lv2render.c -o lv2render -ldl
*/

#include <lv2/atom/atom.h>
#include <lv2/buf-size/buf-size.h>
#include <lv2/core/lv2.h>
#include <lv2/options/options.h>
#include <lv2/urid/urid.h>

#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { block = 256, maxPorts = 64, maxUris = 128 };

static float* readWav (const char* path, uint32_t* numSamples, uint32_t* rate)
{
    FILE* f = fopen (path, "rb");
    if (! f) { perror (path); exit (1); }

    char riff[12];
    if (fread (riff, 1, 12, f) != 12 || memcmp (riff, "RIFF", 4) || memcmp (riff + 8, "WAVE", 4))
        { fprintf (stderr, "%s: not a WAV\n", path); exit (1); }

    uint16_t format = 0, channels = 0, bits = 0;
    float* samples = NULL;

    for (;;)
    {
        char id[4];
        uint32_t size;
        if (fread (id, 1, 4, f) != 4 || fread (&size, 4, 1, f) != 1)
            break;

        if (! memcmp (id, "fmt ", 4))
        {
            unsigned char fmt[16];
            if (size < 16 || fread (fmt, 1, 16, f) != 16) break;
            memcpy (&format, fmt, 2);
            memcpy (&channels, fmt + 2, 2);
            memcpy (rate, fmt + 4, 4);
            memcpy (&bits, fmt + 14, 2);
            fseek (f, (long) (size - 16 + (size & 1)), SEEK_CUR);
        }
        else if (! memcmp (id, "data", 4))
        {
            *numSamples = size / 4;
            samples = malloc (size);
            if (fread (samples, 4, *numSamples, f) != *numSamples) { free (samples); samples = NULL; }
            break;
        }
        else
        {
            fseek (f, (long) (size + (size & 1)), SEEK_CUR);
        }
    }

    fclose (f);

    if (! samples || format != 3 || channels != 1 || bits != 32)
        { fprintf (stderr, "%s: needs to be a mono 32-bit float WAV\n", path); exit (1); }

    return samples;
}

static void writeWav (const char* path, const float* samples, uint32_t numSamples, uint32_t rate)
{
    FILE* f = fopen (path, "wb");
    if (! f) { perror (path); exit (1); }

    const uint32_t dataSize = numSamples * 4, fmtSize = 16, riffSize = 4 + 8 + fmtSize + 8 + dataSize;
    const uint16_t format = 3, channels = 1, blockAlign = 4, bits = 32;
    const uint32_t byteRate = rate * 4;

    fwrite ("RIFF", 1, 4, f); fwrite (&riffSize, 4, 1, f); fwrite ("WAVE", 1, 4, f);
    fwrite ("fmt ", 1, 4, f); fwrite (&fmtSize, 4, 1, f);
    fwrite (&format, 2, 1, f); fwrite (&channels, 2, 1, f); fwrite (&rate, 4, 1, f);
    fwrite (&byteRate, 4, 1, f); fwrite (&blockAlign, 2, 1, f); fwrite (&bits, 2, 1, f);
    fwrite ("data", 1, 4, f); fwrite (&dataSize, 4, 1, f);
    fwrite (samples, 4, numSamples, f);
    fclose (f);
}

// urid:map: each URI gets a number, the same one every time it's asked for.
static char* uris[maxUris];
static int numUris = 0;

static LV2_URID mapUri (LV2_URID_Map_Handle handle, const char* uri)
{
    (void) handle;

    for (int i = 0; i < numUris; ++i)
        if (! strcmp (uris[i], uri))
            return (LV2_URID) (i + 1);

    if (numUris == maxUris) { fprintf (stderr, "too many URIs\n"); exit (1); }

    uris[numUris] = strdup (uri);
    return (LV2_URID) ++numUris;
}

static int portArg (const char* arg, const char* name, uint32_t* port)
{
    const size_t n = strlen (name);
    if (strncmp (arg, name, n) || arg[n] != '=')
        return 0;

    *port = (uint32_t) atoi (arg + n + 1);
    return 1;
}

int main (int argc, char** argv)
{
    if (argc < 6)
    {
        fprintf (stderr, "usage: %s PLUGIN.so IN.wav OUT.wav PREROLL in=PORT out=PORT [ARG...]\n", argv[0]);
        return 1;
    }

    void* lib = dlopen (argv[1], RTLD_NOW | RTLD_LOCAL);
    if (! lib) { fprintf (stderr, "%s\n", dlerror()); return 1; }

    LV2_Descriptor_Function getDescriptor = (LV2_Descriptor_Function) dlsym (lib, "lv2_descriptor");
    const LV2_Descriptor* descriptor = getDescriptor ? getDescriptor (0) : NULL;
    if (! descriptor) { fprintf (stderr, "%s: no LV2 plugin in it\n", argv[1]); return 1; }

    uint32_t numSamples = 0, rate = 0;
    float* input = readWav (argv[2], &numSamples, &rate);
    const uint32_t preroll = (uint32_t) atoi (argv[4]);

    // The features.
    LV2_URID_Map map = { NULL, mapUri };
    const LV2_Feature mapFeature = { LV2_URID__map, &map };
    const LV2_Feature boundedFeature = { LV2_BUF_SIZE__boundedBlockLength, NULL };
    static int32_t blockLength = block;
    const LV2_Options_Option options[] = {
        { LV2_OPTIONS_INSTANCE, 0, mapUri (NULL, LV2_BUF_SIZE__maxBlockLength), sizeof (int32_t), mapUri (NULL, LV2_ATOM__Int), &blockLength },
        { LV2_OPTIONS_INSTANCE, 0, mapUri (NULL, LV2_BUF_SIZE__minBlockLength), sizeof (int32_t), mapUri (NULL, LV2_ATOM__Int), &blockLength },
        { LV2_OPTIONS_INSTANCE, 0, 0, 0, 0, NULL },
    };
    const LV2_Feature optionsFeature = { LV2_OPTIONS__options, (void*) options };
    const LV2_Feature* features[] = { &mapFeature, &boundedFeature, &optionsFeature, NULL };

    LV2_Handle plugin = descriptor->instantiate (descriptor, rate, "", features);
    if (! plugin) { fprintf (stderr, "couldn't instantiate %s\n", descriptor->URI); return 1; }

    static float controls[maxPorts];
    static float in[block], out[block], silence[block], ignored[block];
    // An empty event sequence, for atom inputs.
    static LV2_Atom_Sequence noEvents;
    noEvents.atom.size = sizeof (LV2_Atom_Sequence_Body);
    noEvents.atom.type = mapUri (NULL, LV2_ATOM__Sequence);

    int haveIn = 0, haveOut = 0;

    for (int i = 5; i < argc; ++i)
    {
        uint32_t port;
        float value;

        if (portArg (argv[i], "in", &port))            { descriptor->connect_port (plugin, port, in); haveIn = 1; }
        else if (portArg (argv[i], "out", &port))      { descriptor->connect_port (plugin, port, out); haveOut = 1; }
        else if (portArg (argv[i], "silent", &port))   descriptor->connect_port (plugin, port, silence);
        else if (portArg (argv[i], "unused", &port))   descriptor->connect_port (plugin, port, ignored);
        else if (portArg (argv[i], "atom", &port))     descriptor->connect_port (plugin, port, &noEvents);
        else if (sscanf (argv[i], "%u=%f", &port, &value) == 2 && port < maxPorts)
        {
            controls[port] = value;
            descriptor->connect_port (plugin, port, &controls[port]);
        }
        else
        {
            fprintf (stderr, "bad argument: %s\n", argv[i]);
            return 1;
        }
    }

    if (! haveIn || ! haveOut) { fprintf (stderr, "in= and out= are needed\n"); return 1; }

    if (descriptor->activate)
        descriptor->activate (plugin);

    float* output = malloc ((size_t) numSamples * 4);
    const uint32_t total = preroll + numSamples;

    for (uint32_t done = 0; done < total; done += block)
    {
        const uint32_t n = total - done < block ? total - done : block;

        for (uint32_t i = 0; i < n; ++i)
            in[i] = done + i < preroll ? 0.0f : input[done + i - preroll];

        descriptor->run (plugin, n);

        for (uint32_t i = 0; i < n; ++i)
            if (done + i >= preroll)
                output[done + i - preroll] = out[i];
    }

    if (descriptor->deactivate)
        descriptor->deactivate (plugin);

    descriptor->cleanup (plugin);
    writeWav (argv[3], output, numSamples, rate);
    printf ("%s -> %s (%s)\n", argv[2], argv[3], descriptor->URI);
    return 0;
}
