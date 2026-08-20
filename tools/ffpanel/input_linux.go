// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * input_linux.go — Linux FF UAPI device I/O for ffpanel.
 *
 * The tool speaks ONLY the evdev force-feedback UAPI (EVIOCSFF /
 * EVIOCRMFF / EV_FF writes and the EVIOCG* queries); it never touches
 * raw HID — wire safety is the driver's job (work/analysis/13).
 *
 * The ff_effect marshalling matches struct ff_effect from
 * include/uapi/linux/input.h on 64-bit hosts (48 bytes; union at
 * offset 16). input_event writes are 24 bytes with a zeroed timeval,
 * exactly what the C ffctl wrote.
 */
package main

import (
	"encoding/binary"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"unsafe"

	"golang.org/x/sys/unix"
)

// FF UAPI constants (include/uapi/linux/input-event-codes.h).
const (
	evFF         = 0x15
	ffRumble     = 0x50
	ffPeriodic   = 0x51
	ffConstant   = 0x52
	ffSpring     = 0x53
	ffFriction   = 0x54
	ffDamper     = 0x55
	ffInertia    = 0x56
	ffRamp       = 0x57
	ffSquare     = 0x58
	ffTriangle   = 0x59
	ffSine       = 0x5a
	ffSawUp      = 0x5b
	ffSawDown    = 0x5c
	ffGain       = 0x60
	ffAutocenter = 0x61
)

// ioctl request encoding: _IOC(dir,type,nr,size) on Linux
// (dir: 1=write, 2=read; type 'E' = input).
const (
	eviocgID   = uintptr(2<<30 | 8<<16 | 'E'<<8 | 0x02)   // _IOR('E', 0x02, struct input_id)
	eviocsff   = uintptr(1<<30 | 48<<16 | 'E'<<8 | 0x80)  // _IOW('E', 0x80, struct ff_effect)
	eviocrmff  = uintptr(1<<30 | 4<<16 | 'E'<<8 | 0x81)   // _IOW('E', 0x81, int)
	eviocgName = uintptr(2<<30 | 256<<16 | 'E'<<8 | 0x06) // _IOR('E', 0x06, char[256])
)

func eviocgbit(ev byte, size uintptr) uintptr {
	return uintptr(2<<30 | int(size)<<16 | 'E'<<8 | 0x20) + uintptr(ev)
}

var le = binary.LittleEndian

// InputID mirrors struct input_id.
type InputID struct {
	Bustype uint16
	Vendor  uint16
	Product uint16
	Version uint16
}

// DeviceInfo is one scanned /dev/input/event* node.
type DeviceInfo struct {
	Path    string
	Name    string
	ID      InputID
	FFBits  []byte // EVIOCGBIT(EV_FF) bitmap
	HasFF   bool
	FFCaps  []string
	WaveCap map[string]bool // periodic waveforms the device advertises
}

// Has reports whether FF capability bit code is advertised.
func (d *DeviceInfo) Has(code int) bool {
	return d.FFBits[code/8]&(1<<(code%8)) != 0
}

// SupportsType reports whether effect type string ("sine", "constant",
// ...) is offered by this device.
func (d *DeviceInfo) SupportsType(t string) bool {
	switch t {
	case "constant":
		return d.Has(ffConstant)
	case "ramp":
		return d.Has(ffRamp)
	case "rumble":
		return d.Has(ffRumble)
	case "sine":
		return d.Has(ffPeriodic) && d.Has(ffSine)
	case "square":
		return d.Has(ffPeriodic) && d.Has(ffSquare)
	case "triangle":
		return d.Has(ffPeriodic) && d.Has(ffTriangle)
	case "sawup":
		return d.Has(ffPeriodic) && d.Has(ffSawUp)
	case "sawdown":
		return d.Has(ffPeriodic) && d.Has(ffSawDown)
	}
	return false
}

func ioctl(fd int, req uintptr, buf unsafe.Pointer) error {
	_, _, errno := unix.Syscall(unix.SYS_IOCTL, uintptr(fd), req,
		uintptr(buf))
	if errno != 0 {
		return errno
	}
	return nil
}

// ScanDevices enumerates /dev/input/event* nodes and their FF caps.
// Non-FF devices are included (greyed out in the UI); nodes that cannot
// be opened at all are skipped.
func ScanDevices() []DeviceInfo {
	paths, _ := filepath.Glob("/dev/input/event*")
	devs := []DeviceInfo{}
	for _, p := range paths {
		fd, err := unix.Open(p, unix.O_RDONLY|unix.O_NONBLOCK, 0)
		if err != nil {
			continue
		}
		info := DeviceInfo{Path: p, WaveCap: map[string]bool{}}
		var nameBuf [256]byte
		if err := ioctl(fd, eviocgName, unsafe.Pointer(&nameBuf[0])); err == nil {
			for i, b := range nameBuf {
				if b == 0 {
					info.Name = string(nameBuf[:i])
					break
				}
			}
		}
		var idBuf [8]byte
		if err := ioctl(fd, eviocgID, unsafe.Pointer(&idBuf[0])); err == nil {
			info.ID = InputID{
				Bustype: le.Uint16(idBuf[0:]),
				Vendor:  le.Uint16(idBuf[2:]),
				Product: le.Uint16(idBuf[4:]),
				Version: le.Uint16(idBuf[6:]),
			}
		}
		// event-type bitmap first: is EV_FF offered at all?
		var evBits [4]byte
		if err := ioctl(fd, eviocgbit(0, 4), unsafe.Pointer(&evBits[0])); err == nil {
			info.HasFF = evBits[evFF/8]&(1<<(evFF%8)) != 0
		}
		if info.HasFF {
			ffBits := make([]byte, 16) // covers up to code 0x7f
			if err := ioctl(fd, eviocgbit(evFF, 16),
				unsafe.Pointer(&ffBits[0])); err == nil {
				info.FFBits = ffBits
			}
		}
		unix.Close(fd)

		if info.HasFF {
			for _, c := range []struct {
				code int
				name string
			}{
				{ffConstant, "constant"}, {ffPeriodic, "periodic"},
				{ffRamp, "ramp"}, {ffSpring, "spring"},
				{ffFriction, "friction"}, {ffDamper, "damper"},
				{ffInertia, "inertia"}, {ffRumble, "rumble"},
				{ffGain, "gain"}, {ffAutocenter, "autocenter"},
			} {
				if info.Has(c.code) {
					info.FFCaps = append(info.FFCaps, c.name)
				}
			}
			for _, w := range []struct {
				code int
				name string
			}{
				{ffSquare, "square"}, {ffTriangle, "triangle"},
				{ffSine, "sine"}, {ffSawUp, "sawup"},
				{ffSawDown, "sawdown"},
			} {
				if info.Has(w.code) {
					info.WaveCap[w.name] = true
				}
			}
		}
		devs = append(devs, info)
	}
	sort.Slice(devs, func(i, j int) bool { return devs[i].Path < devs[j].Path })
	return devs
}

// EffectParams is the tool-side effect representation shared by the
// one-shot CLI and the TUI. Field defaults mirror the C ffctl.
type EffectParams struct {
	Kind         string // sine|square|triangle|sawup|sawdown|constant|ramp|rumble
	Period       int    // ms, periodic only
	Magnitude    int    // 0..32767; also constant level
	Offset       int    // periodic offset
	Phase        int    // centidegrees, periodic
	Direction    int    // 0..65535; 0 = zero force here
	Duration     int    // ms, 0 = until stopped
	Count        int    // play repetitions
	Delay        int    // start delay ms
	Attack       int    // envelope attack ms
	AttackLevel  int    // 0..32767
	Fade         int    // envelope fade ms
	FadeLevel    int    // 0..32767
	Start        int    // ramp start level
	End          int    // ramp end level
	Strong       int    // rumble strong magnitude
	Weak         int    // rumble weak magnitude
}

// DefaultParams returns ffctl's defaults.
func DefaultParams(kind string) EffectParams {
	return EffectParams{
		Kind:      kind,
		Period:    1000,
		Magnitude: 20000,
		Direction: 16384,
		Duration:  3000,
		Count:     1,
		Start:     -10000,
		End:       10000,
		Strong:    20000,
		Weak:      10000,
	}
}

func u16(v int) uint16 {
	if v < 0 {
		v += 0x10000
	}
	return uint16(v & 0xffff)
}

// Marshal builds the 48-byte little-endian struct ff_effect image the
// kernel expects (union at offset 16; see file comment).
func (p *EffectParams) Marshal(id int16) [48]byte {
	var b [48]byte
	var ftype uint16
	switch p.Kind {
	case "constant":
		ftype = ffConstant
	case "ramp":
		ftype = ffRamp
	case "rumble":
		ftype = ffRumble
	case "gain":
		ftype = ffGain
	case "autocenter":
		ftype = ffAutocenter
	default:
		ftype = ffPeriodic
	}
	le.PutUint16(b[0:], ftype)
	le.PutUint16(b[2:], uint16(id))
	le.PutUint16(b[4:], u16(p.Direction))
	le.PutUint16(b[10:], u16(p.Duration))
	le.PutUint16(b[12:], u16(p.Delay))

	putEnv := func(off int) {
		le.PutUint16(b[off:], u16(p.Attack))
		le.PutUint16(b[off+2:], u16(p.AttackLevel))
		le.PutUint16(b[off+4:], u16(p.Fade))
		le.PutUint16(b[off+6:], u16(p.FadeLevel))
	}
	switch ftype {
	case ffConstant:
		le.PutUint16(b[16:], u16(p.Magnitude))
		putEnv(18)
	case ffRamp:
		le.PutUint16(b[16:], u16(p.Start))
		le.PutUint16(b[18:], u16(p.End))
		putEnv(20)
	case ffRumble:
		le.PutUint16(b[16:], u16(p.Strong))
		le.PutUint16(b[18:], u16(p.Weak))
	case ffPeriodic:
		wf := uint16(ffSine)
		switch p.Kind {
		case "square":
			wf = ffSquare
		case "triangle":
			wf = ffTriangle
		case "sawup":
			wf = ffSawUp
		case "sawdown":
			wf = ffSawDown
		}
		le.PutUint16(b[16:], wf)
		le.PutUint16(b[18:], u16(p.Period))
		le.PutUint16(b[20:], u16(p.Magnitude))
		le.PutUint16(b[22:], u16(p.Offset))
		le.PutUint16(b[24:], u16(p.Phase))
		putEnv(26)
	}
	return b
}

// ToFx converts to the monitor's parity model. Rumble goes through the
// same rumble->sine conversion the parent driver applies at upload.
func (p *EffectParams) ToFx() Fx {
	if p.Kind == "rumble" {
		return RumbleConvert(p.Strong, p.Weak, uint32(p.Duration))
	}
	return Fx{
		Type:       p.Kind,
		Magnitude:  p.Magnitude,
		Offset:     p.Offset,
		PhaseCd:    uint32(p.Phase),
		PeriodMs:   uint32(p.Period),
		Direction:  uint16(p.Direction & 0xffff),
		Env:        Envelope{AttackLength: u16(p.Attack), AttackLevel: u16(p.AttackLevel), FadeLength: u16(p.Fade), FadeLevel: u16(p.FadeLevel)},
		DelayMs:    uint32(p.Delay),
		LengthMs:   uint32(p.Duration),
		Count:      uint32(p.Count),
		StartLevel: p.Start,
		EndLevel:   p.End,
	}
}

// Device is an open event node with at most one uploaded effect plus
// the FF_GAIN/FF_AUTOCENTER pseudo-effects.
type Device struct {
	fd        int
	Info      DeviceInfo
	effectID  int16
	uploaded  bool
	pseudoIDs []int16
}

// Open opens the event node read-write (upload/play needs O_RDWR).
func Open(path string) (*Device, error) {
	fd, err := unix.Open(path, unix.O_RDWR, 0)
	if err != nil {
		return nil, &os.PathError{Op: "open", Path: path, Err: err}
	}
	d := &Device{fd: fd}
	for _, cand := range ScanDevices() {
		if cand.Path == path {
			d.Info = cand
			break
		}
	}
	if d.Info.Path == "" {
		d.Info.Path = path
	}
	return d, nil
}

// Close stops and erases everything this tool uploaded, then closes.
// No residual torque survives the tool.
func (d *Device) Close() error {
	var firstErr error
	keep := func(err error) {
		if err != nil && firstErr == nil {
			firstErr = err
		}
	}
	if d.uploaded {
		keep(d.Stop())
		keep(d.Erase())
	}
	for _, id := range d.pseudoIDs {
		keep(d.eraseID(id))
	}
	d.pseudoIDs = nil
	keep(unix.Close(d.fd))
	return firstErr
}

// Upload uploads a fresh effect (id -1) and remembers its id.
func (d *Device) Upload(p *EffectParams) (int16, error) {
	if d.uploaded {
		return d.effectID, d.Update(p)
	}
	buf := p.Marshal(-1)
	if err := ioctl(d.fd, eviocsff, unsafe.Pointer(&buf[0])); err != nil {
		return -1, fmt.Errorf("EVIOCSFF (effect upload): %w", err)
	}
	d.effectID = int16(le.Uint16(buf[2:]))
	d.uploaded = true
	return d.effectID, nil
}

// Update re-uploads with the existing id — the driver's update_effect
// path, which sends only changed parameter packets. Never implemented
// as erase+create (that would churn ids and slot-0 state).
func (d *Device) Update(p *EffectParams) error {
	if !d.uploaded {
		_, err := d.Upload(p)
		return err
	}
	buf := p.Marshal(d.effectID)
	if err := ioctl(d.fd, eviocsff, unsafe.Pointer(&buf[0])); err != nil {
		return fmt.Errorf("EVIOCSFF (effect update): %w", err)
	}
	d.effectID = int16(le.Uint16(buf[2:]))
	return nil
}

// Reupload erases and recreates the effect ([u] escape hatch when the
// update path is suspected stale).
func (d *Device) Reupload(p *EffectParams) (int16, error) {
	if d.uploaded {
		_ = d.Stop()
		if err := d.Erase(); err != nil {
			return -1, err
		}
	}
	return d.Upload(p)
}

// writeFF writes an EV_FF input_event (24 bytes, zeroed timeval).
func (d *Device) writeFF(code int16, value int32) error {
	var ev [24]byte
	le.PutUint16(ev[16:], evFF)
	le.PutUint16(ev[18:], uint16(code))
	le.PutUint32(ev[20:], uint32(value))
	_, err := unix.Write(d.fd, ev[:])
	if err != nil {
		return fmt.Errorf("EV_FF write: %w", err)
	}
	return nil
}

// Play starts playback (value = repeat count).
func (d *Device) Play(count int) error {
	if !d.uploaded {
		return errors.New("play: no effect uploaded")
	}
	return d.writeFF(d.effectID, int32(count))
}

// Stop stops playback (EV_FF value 0).
func (d *Device) Stop() error {
	if !d.uploaded {
		return nil
	}
	return d.writeFF(d.effectID, 0)
}

// Erase removes the uploaded effect (EVIOCRMFF).
func (d *Device) Erase() error {
	if !d.uploaded {
		return nil
	}
	id := d.effectID
	d.uploaded = false
	d.effectID = -1
	return d.eraseID(id)
}

func (d *Device) eraseID(id int16) error {
	_, _, errno := unix.Syscall(unix.SYS_IOCTL, uintptr(d.fd), eviocrmff,
		uintptr(unsafe.Pointer(&id)))
	if errno != 0 {
		return errno
	}
	return nil
}

// setPseudo uploads an FF_GAIN / FF_AUTOCENTER pseudo-effect and plays
// it with value = pct*65535/100 (fftest convention), then erases it.
// The level sticks in the device; nothing stays uploaded.
func (d *Device) setPseudo(ftype uint16, pct int) error {
	var b [48]byte
	le.PutUint16(b[0:], ftype)
	le.PutUint16(b[2:], 0xffff) // id -1
	if err := ioctl(d.fd, eviocsff, unsafe.Pointer(&b[0])); err != nil {
		return fmt.Errorf("EVIOCSFF (pseudo 0x%x): %w", ftype, err)
	}
	id := int16(le.Uint16(b[2:]))
	d.pseudoIDs = append(d.pseudoIDs, id)
	value := int32(pct * 65535 / 100)
	err := d.writeFF(id, value)
	if rmErr := d.eraseID(id); rmErr != nil && err == nil {
		err = rmErr
	}
	for i, pid := range d.pseudoIDs {
		if pid == id {
			d.pseudoIDs = append(d.pseudoIDs[:i], d.pseudoIDs[i+1:]...)
			break
		}
	}
	return err
}

// SetGain sets the device gain in percent (0..100).
func (d *Device) SetGain(pct int) error { return d.setPseudo(ffGain, pct) }

// SetAutocenter sets autocenter strength in percent (0 = disabled).
func (d *Device) SetAutocenter(pct int) error {
	return d.setPseudo(ffAutocenter, pct)
}
