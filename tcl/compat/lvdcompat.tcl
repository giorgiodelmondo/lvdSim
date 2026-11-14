# SPDX-License-Identifier: GPL-3.0-or-later
# Familiar simulator commands for the NVC interactive shell.
#
# Loaded by `vsim -c` before the user's script. It maps the common simulation control commands onto the
# native NVC shell commands and accepts (without effect) the ones that only matter in a GUI, so that
# existing .do scripts keep running. Written from the behaviour scripts rely on; see docs/command-compat.md
# for what is native, wrapped, accepted or unsupported.

namespace eval ::lvd {
    variable version 1
    # The GUI sets this to 1 before sourcing this file; commands that only make sense in a GUI then tell
    # it about themselves with a "@@lvd ..." line on stdout, which the GUI removes from the transcript.
    if {![info exists gui]} { variable gui 0 }
    variable defaultRunFs 100000000   ;# 100 ns, the usual default run length

    proc warn {text} { puts stderr "** Warning: (lvdsim) $text" }

    proc notify {args} {
        variable gui
        if {$gui} {
            puts "@@lvd [list {*}$args]"
            flush stdout
        }
    }

    proc unsupported {name hint} {
        return -code error "$name is not supported yet ($hint)"
    }

    # "100 ns", "100ns", "1.5 us", "0.25ms" -> integer femtoseconds
    proc timeToFs {args} {
        set text [string trim [join $args " "]]
        if {![regexp -nocase {^([0-9]*\.?[0-9]+)\s*(fs|ps|ns|us|ms|sec|s)$} $text -> value unit]} {
            return -code error "invalid time \"$text\""
        }
        switch -- [string tolower $unit] {
            fs      { set mult 1 }
            ps      { set mult 1000 }
            ns      { set mult 1000000 }
            us      { set mult 1000000000 }
            ms      { set mult 1000000000000 }
            default { set mult 1000000000000000 }
        }
        return [expr {wide(round($value * $mult))}]
    }

    # The familiar flow accepts 16#FF, 2#1010, 0xFF and plain numbers; the native force takes plain values.
    proc forceValue {v} {
        if {[regexp {^([0-9]+)#([0-9a-fA-F_]+)$} $v -> base digits]} {
            set digits [string map {_ {}} $digits]
            switch -- $base {
                2  { return [expr {wide("0b$digits")}] }
                8  { return [expr {wide("0o$digits")}] }
                10 { return [expr {wide($digits)}] }
                16 { return [expr {wide("0x$digits")}] }
                default { return -code error "unsupported number base $base in \"$v\"" }
            }
        }
        if {[regexp -nocase {^0x[0-9a-f]+$} $v]} { return [expr {wide($v)}] }
        return $v
    }
}

# --- run ---------------------------------------------------------------------------------------
rename run ::lvd::nativeRun

# True while a process is stopped at a source line (the native stack command only works then).
proc ::lvd::stoppedInProcess {} {
    expr {[llength [info commands ::stack]] > 0 && ![catch {::stack}]}
}

# While a process is stopped at a source line, run without a time (and run -continue) resumes the
# suspended run; a time is refused by the kernel then, with its own message.
proc run {args} {
    set all 0
    set rest {}
    set json {}
    foreach a $args {
        switch -- $a {
            -all      { set all 1 }
            -json     { set json {-json} }
            -continue { if {[::lvd::stoppedInProcess]} { set all 1 } }
            -step - -next - -over - -out {
                return -code error "run $a is not supported: use step -into, step -over or step -out"
            }
            default   { lappend rest $a }
        }
    }
    if {$all || ([llength $rest] == 0 && [::lvd::stoppedInProcess])} {
        ::lvd::nativeRun {*}$json
    } elseif {[llength $rest] == 0} {
        ::lvd::nativeRun {*}$json $::lvd::defaultRunFs fs
    } else {
        ::lvd::nativeRun {*}$json [::lvd::timeToFs {*}$rest] fs
    }
}

# --- force / noforce ---------------------------------------------------------------------------
rename force ::lvd::nativeForce

proc force {args} {
    set positional {}
    set i 0
    while {$i < [llength $args]} {
        set a [lindex $args $i]
        if {[llength $positional] == 0 && [string match -* $a]} {
            if {$a eq "-cancel" || $a eq "-repeat"} { incr i }   ;# these take a time argument
            incr i
            continue
        }
        lappend positional $a
        incr i
    }
    if {[llength $positional] == 0} { return [::lvd::nativeForce] }   ;# list the forced signals
    if {[llength $positional] < 2} {
        return -code error "usage: force \[-freeze|-drive|-deposit\] <signal> <value> \[<time> ...\]"
    }
    if {[llength $positional] > 2} {
        ::lvd::warn "force with a time list is not supported, applying the first value now"
    }
    ::lvd::nativeForce [lindex $positional 0] [::lvd::forceValue [lindex $positional 1]]
}

# --- leaving -----------------------------------------------------------------------------------
proc quit {args} {
    set code 0
    for {set i 0} {$i < [llength $args]} {incr i} {
        set a [lindex $args $i]
        if {$a eq "-code"} {
            incr i
            set code [lindex $args $i]
        }
        ;# -f, -force, -sim and -noprompt are accepted and need no action
    }
    exit -code $code
}

# --- commands that need a GUI or are accepted without effect ----------------------------------
proc add {what args} {
    # In the GUI "add wave" fills the wave window, "add list" the List dock and "add mem" shows a memory in the
    # Memory dock; in batch mode waveforms come from -wlf or -lvdwave.
    ::lvd::notify add $what {*}$args
}
proc delete {what args} { ::lvd::notify delete $what {*}$args }
proc log {args} { return }
proc wave {args} { return }
proc view {args} { ::lvd::notify view {*}$args }   ;# GUI: shows the named windows (view memory, view list)
proc radix {args} { return }
proc configure {args} { return }
proc transcript {args} { return }
proc onerror {args} { return }
proc onbreak {args} { return }
proc onElabError {args} { return }
proc quietly {args} { uplevel 1 $args }

# --- do ----------------------------------------------------------------------------------------
proc do {file args} {
    set ::argv $args
    uplevel #0 [list source $file]
}

# --- not available yet -------------------------------------------------------------------------
proc when {args} { ::lvd::unsupported when "needs kernel support" }
proc bp {args} { ::lvd::unsupported bp "needs kernel support" }
# step and breakpoint are native commands of the fork (patch 0005)
proc vcd {args} { ::lvd::unsupported vcd "use vsim -wlf or -lvdwave to record a waveform" }
