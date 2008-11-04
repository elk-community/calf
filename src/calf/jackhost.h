/* Calf DSP Library Utility Application - calfjackhost
 * API wrapper for JACK Audio Connection Kit
 *
 * Copyright (C) 2007 Krzysztof Foltman
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */
#ifndef __CALF_JACKHOST_H
#define __CALF_JACKHOST_H

#if USE_JACK

#include <jack/jack.h>
#include <jack/midiport.h>
#include "gui.h"
#include "utils.h"
#include <pthread.h>

namespace synth {

class jack_host_base;
    
class jack_client {
protected:
    std::vector<jack_host_base *> plugins;
    calf_utils::ptmutex mutex;
public:
    jack_client_t *client;
    int input_nr, output_nr, midi_nr;
    std::string input_name, output_name, midi_name;
    int sample_rate;

    jack_client()
    {
        input_nr = output_nr = midi_nr = 1;
        input_name = "input_%d";
        output_name = "output_%d";
        midi_name = "midi_%d";
        sample_rate = 0;
        client = NULL;
    }
    
    void add(jack_host_base *plugin)
    {
        calf_utils::ptlock lock(mutex);
        plugins.push_back(plugin);
    }
    
    void del(int item)
    {
        calf_utils::ptlock lock(mutex);
        plugins.erase(plugins.begin()+item);
    }
    
    void open(const char *client_name)
    {
        jack_status_t status;
        client = jack_client_open(client_name, JackNullOption, &status);
        if (!client)
            throw audio_exception("Could not initialize JACK subsystem");
        sample_rate = jack_get_sample_rate(client);
        jack_set_process_callback(client, do_jack_process, this);
        jack_set_buffer_size_callback(client, do_jack_bufsize, this);
    }
    
    std::string get_name()
    {
        return std::string(jack_get_client_name(client));
    }
    
    void activate()
    {
        jack_activate(client);        
    }

    void deactivate()
    {
        jack_deactivate(client);        
    }
    
    void delete_plugins();
    
    void connect(const std::string &p1, const std::string &p2)
    {
        if (jack_connect(client, p1.c_str(), p2.c_str()) != 0)
            throw audio_exception("Could not connect JACK ports "+p1+" and "+p2);
    }
    
    void close()
    {
        jack_client_close(client);
    }
    
    static int do_jack_process(jack_nframes_t nframes, void *p);
    static int do_jack_bufsize(jack_nframes_t numsamples, void *p);
};
    
class jack_host_base: public plugin_ctl_iface {
public:
    typedef int (*process_func)(jack_nframes_t nframes, void *p);
    struct port {
        jack_port_t *handle;
        float *data;
        std::string name;
        port() : handle(NULL), data(NULL) {}
        ~port() { }
    };
    jack_client *client;
    bool changed;
    port midi_port;
    std::string name;
    virtual port *get_inputs()=0;
    virtual port *get_outputs()=0;
    virtual port *get_midi_port() { return get_midi() ? &midi_port : NULL; }
    virtual float *get_params()=0;
    virtual void init_module()=0;
    virtual void cache_ports()=0;
    virtual int process(jack_nframes_t nframes)=0;
    
    jack_host_base() {
        client = NULL;
        changed = true;
    }
    
    void set_params(const float *params) {
        memcpy(get_params(), params, get_param_count() * sizeof(float));
        changed = true;
    }

    void set_params() {
        changed = true;
    }
    
    void open(jack_client *_client)
    {
        client = _client; //jack_client_open(client_name, JackNullOption, &status);
        
        create_ports();
        
        cache_ports();
        
        init_module();
        changed = false;
    }
    
    virtual void create_ports() {
        char buf[32];
        port *inputs = get_inputs();
        port *outputs = get_outputs();
        int in_count = get_input_count(), out_count = get_output_count();
        for (int i=0; i<in_count; i++) {
            sprintf(buf, client->input_name.c_str(), client->input_nr++);
            inputs[i].name = buf;
            inputs[i].handle = jack_port_register(client->client, buf, JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput , 0);
            inputs[i].data = NULL;
            if (!inputs[i].handle)
                throw audio_exception("Could not create JACK input port");
        }
        for (int i=0; i<out_count; i++) {
            sprintf(buf, client->output_name.c_str(), client->output_nr++);
            outputs[i].name = buf;
            outputs[i].handle = jack_port_register(client->client, buf, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput , 0);
            outputs[i].data = NULL;
            if (!outputs[i].handle)
                throw audio_exception("Could not create JACK output port");
        }
        if (get_midi()) {
            sprintf(buf, client->midi_name.c_str(), client->midi_nr++);
            midi_port.name = buf;
            midi_port.handle = jack_port_register(client->client, buf, JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
            if (!midi_port.handle)
                throw audio_exception("Could not create JACK MIDI port");
        }
    }
    
    void close() {
        port *inputs = get_inputs(), *outputs = get_outputs();
        int input_count = get_input_count(), output_count = get_output_count();
        for (int i = 0; i < input_count; i++) {
            jack_port_unregister(client->client, inputs[i].handle);
            inputs[i].data = NULL;
        }
        for (int i = 0; i < output_count; i++) {
            jack_port_unregister(client->client, outputs[i].handle);
            outputs[i].data = NULL;
        }
        if (get_midi())
            jack_port_unregister(client->client, midi_port.handle);
        client = NULL;
    }

    virtual ~jack_host_base() {
    }
};

struct vumeter
{
    float level, falloff;
    
    vumeter()
    {
        falloff = 0.999f;
        level = 0;
    }
    
    inline void update(float *src, unsigned int len)
    {
        double tmp = level;
        for (unsigned int i = 0; i < len; i++)
            tmp = std::max(tmp * falloff, (double)fabs(src[i]));
        level = tmp;
        dsp::sanitize(level);
    }
    inline void update_zeros(unsigned int len)
    {
        level *= pow((double)falloff, (double)len);
        dsp::sanitize(level);
    }
};

template<class Module>
class jack_host: public jack_host_base, public Module {
public:
    using Module::in_count;
    using Module::out_count;
    using Module::param_count;

    port inputs[in_count], outputs[out_count];
    vumeter input_vus[in_count], output_vus[out_count];
    float param_values[param_count];
    float midi_meter;
    
    jack_host()
    {
        for (int i = 0; i < Module::param_count; i++) {
            param_values[i] = Module::param_props[i].def_value;
            Module::params[i] = &param_values[i];
        }
        midi_meter = 0;
    }
    
    ~jack_host()
    {
        if (client)
            close();
    }
    
    virtual void init_module() {
        Module::set_sample_rate(client->sample_rate);
        Module::activate();
        Module::params_changed();
    }

    virtual synth::parameter_properties* get_param_props(int param_no) { return Module::param_props + param_no; }
    
    void handle_event(uint8_t *buffer, uint32_t size)
    {
        int value;
        switch(buffer[0] >> 4)
        {
        case 8:
            Module::note_off(buffer[1], buffer[2]);
            break;
        case 9:
            if (!buffer[2])
                Module::note_off(buffer[1], 0);
            else
                Module::note_on(buffer[1], buffer[2]);
            break;
        case 10:
            Module::program_change(buffer[1]);
            break;
        case 11:
            Module::control_change(buffer[1], buffer[2]);
            break;
        case 14:
            value = buffer[1] + 128 * buffer[2] - 8192;
            Module::pitch_bend(value);
            break;
        }
    }
    void process_part(unsigned int time, unsigned int len)
    {
        if (!len)
            return;
        for (int i = 0; i < in_count; i++)
            input_vus[i].update(Module::ins[i] + time, len);
        unsigned int mask = Module::process(time, len, -1, -1);
        for (int i = 0; i < out_count; i++)
        {
            if (!(mask & (1 << i))) {
                dsp::zero(Module::outs[i] + time, len);
                output_vus[i].update_zeros(len);
            } else
                output_vus[i].update(Module::outs[i] + time, len);
        }
        // decay linearly for 0.1s
        float new_meter = midi_meter - len / (0.1 * client->sample_rate);
        if (new_meter < 0)
            new_meter = 0;
        midi_meter = new_meter;
    }
    virtual float get_level(unsigned int port) { 
        if (port < in_count)
            return input_vus[port].level;
        port -= in_count;
        if (port < out_count)
            return output_vus[port].level;
        port -= out_count;
        if (port == 0 && Module::support_midi)
            return midi_meter;
        return 0.f;
    }
    int process(jack_nframes_t nframes)
    {
        for (int i=0; i<in_count; i++) {
            Module::ins[i] = inputs[i].data = (float *)jack_port_get_buffer(inputs[i].handle, nframes);
        }
        if (Module::support_midi)
            midi_port.data = (float *)jack_port_get_buffer(midi_port.handle, nframes);
        if (changed) {
            Module::params_changed();
            changed = false;
        }

        unsigned int time = 0;
        if (Module::support_midi)
        {
            jack_midi_event_t event;
#ifdef OLD_JACK
            int count = jack_midi_get_event_count(midi_port.data, nframes);
#else
            int count = jack_midi_get_event_count(midi_port.data);
#endif
            for (int i = 0; i < count; i++)
            {
#ifdef OLD_JACK
                jack_midi_event_get(&event, midi_port.data, i, nframes);
#else
                jack_midi_event_get(&event, midi_port.data, i);
#endif
                unsigned int len = event.time - time;
                process_part(time, len);
                
                midi_meter = 1.f;
                handle_event(event.buffer, event.size);
                
                time = event.time;
            }
        }
        process_part(time, nframes - time);
        Module::params_reset();
        return 0;
    }
    
    void cache_ports()
    {
        for (int i=0; i<out_count; i++) {
            Module::outs[i] = outputs[i].data = (float *)jack_port_get_buffer(outputs[i].handle, 0);
        }
    }
    
    virtual void zero_io() {
        memset(inputs, 0, sizeof(inputs));
        memset(outputs, 0, sizeof(outputs));
    }
    
    virtual port *get_inputs() { return inputs; }
    virtual port *get_outputs() { return outputs; }
    virtual float *get_params() { return param_values; }
    virtual bool activate_preset(int bank, int program) { return false; }
    virtual float get_param_value(int param_no) {
        return param_values[param_no];
    }
    virtual void set_param_value(int param_no, float value) {
        param_values[param_no] = value;
        changed = true;
    }
    virtual void execute(int cmd_no) {
        Module::execute(cmd_no);
    }
    virtual char *configure(const char *key, const char *value) { 
        return Module::configure(key, value);
    }
    virtual void send_configures(send_configure_iface *sci) {
        Module::send_configures(sci);
    }
};

extern jack_host_base *create_jack_host(const char *name);

#endif

};

#endif
