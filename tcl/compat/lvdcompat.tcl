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

# --- examine -----------------------------------------------------------------------------------
# The native examine looks names up exactly as written and prints vectors of std_logic in binary whatever
# the radix. VHDL names are not case sensitive (`/dut/REG_DATA` is `/dut/reg_data`) and a radix is expected
# to apply to vectors too, so this wrapper retries a name that is not found in lower case and converts bit
# strings to the radix asked for (hexadecimal, octal, unsigned, decimal/signed). -json is passed through
# unchanged: the GUI uses it with names it got from the kernel.
rename examine ::lvd::nativeExamine
catch {rename exa {}}

namespace eval ::lvd {
    # Radix names of the familiar flow -> hex, oct, unsigned, signed or {} (the native default).
    proc examineRadix {name} {
        switch -- [string tolower [string trimleft $name -]] {
            h - hex - hexadecimal           { return hex }
            o - oct - octal                 { return oct }
            u - unsigned                    { return unsigned }
            d - dec - decimal - s - signed  { return signed }
            b - bin - binary - symbolic     { return {} }
            default { return -code error "unknown radix \"$name\": use binary, octal, hexadecimal, unsigned or decimal" }
        }
    }

    # One name; a name that is not found is tried again in lower case (VHDL). Extended identifiers
    # (\Name\) and Verilog names are case sensitive and found as written.
    proc examineName {opts name} {
        if {![catch {nativeExamine {*}$opts $name} value options]} { return $value }
        set lower [string tolower $name]
        if {$lower ne $name && [string first "\\" $name] < 0 && [string match "*cannot find name*" $value]
            && ![catch {nativeExamine {*}$opts $lower} lowered]} {
            return $lowered
        }
        return -options $options $value
    }

    # Digits of the bit string bits (most significant first) in groups of `width` bits. A group with
    # metavalues shows the metavalue when all its bits have it, else X.
    proc bitsToDigits {bits width} {
        set bits [string map {L 0 H 1 l 0 h 1} [string toupper $bits]]
        set pad [expr {(-[string length $bits]) % $width}]
        set bits [string repeat 0 $pad]$bits
        set out {}
        for {set i 0} {$i < [string length $bits]} {incr i $width} {
            set group [string range $bits $i [expr {$i + $width - 1}]]
            if {[regexp {^[01]+$} $group]} {
                append out [format %X [expr "0b$group"]]
            } elseif {[regexp {^(.)\1*$} $group -> c]} {
                append out $c
            } else {
                append out X
            }
        }
        return $out
    }

    # A bit string ("1010", without the quotes) in the given radix. Unsigned and signed need known bits;
    # with metavalues the bits are returned as they are.
    proc convertBits {bits radix} {
        switch -- $radix {
            hex { return [bitsToDigits $bits 4] }
            oct { return [bitsToDigits $bits 3] }
        }
        set known [string map {L 0 H 1 l 0 h 1} $bits]
        if {![regexp {^[01]+$} $known]} { return "\"$bits\"" }
        set value [expr "0b$known"]
        if {$radix eq "signed" && [string index $known 0] eq "1"} {
            set value [expr {$value - (1 << [string length $known])}]
        }
        return $value
    }

    # Every bit string literal in a value ("1010", also inside an aggregate) converted to the radix.
    proc convertValue {value radix} {
        if {$radix eq {}} { return $value }
        set out {}
        set rest $value
        while {[regexp -indices {"([01UXZWLHuxzwlh-]+)"} $rest whole bits]} {
            append out [string range $rest 0 [expr {[lindex $whole 0] - 1}]]
            append out [convertBits [string range $rest {*}$bits] $radix]
            set rest [string range $rest [expr {[lindex $whole 1] + 1}] end]
        }
        return $out$rest
    }
}

proc examine {args} {
    if {[lsearch -exact $args -json] >= 0} { return [::lvd::nativeExamine {*}$args] }
    set radix {}
    set names {}
    for {set i 0} {$i < [llength $args]} {incr i} {
        set a [lindex $args $i]
        if {[llength $names] == 0 && [string match -* $a]} {
            if {$a eq "-radix"} {
                incr i
                if {$i >= [llength $args]} { return -code error "examine: -radix needs a value" }
                set radix [::lvd::examineRadix [lindex $args $i]]
            } else {
                set radix [::lvd::examineRadix $a]
            }
            continue
        }
        lappend names $a
    }
    if {[llength $names] == 0} {
        return -code error "usage: examine \[-radix binary|octal|hexadecimal|unsigned|decimal\] <name> ..."
    }
    # Integers keep the native hexadecimal format (0x..); everything else is read in the native default.
    set opts [expr {$radix eq "hex" ? {-hex} : {}}]
    set values {}
    foreach n $names {
        lappend values [::lvd::convertValue [::lvd::examineName $opts $n] $radix]
    }
    if {[llength $values] == 1} { return [lindex $values 0] }
    return $values
}
interp alias {} exa {} examine

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
proc quietly {args} { uplevel 1 $args; return }   ;# runs the command without printing its result

# --- do ----------------------------------------------------------------------------------------
# A do script runs one command at a time, and a command's result is printed like a result typed at the
# prompt (`examine /tb/x` in a script shows the value, as in the familiar flow; `source` prints nothing).
# vsim -do and the GUI's -do run the user's script through `do` as well.
proc ::lvd::runScript {file} {
    set f [open $file r]
    set text [read $f]
    close $f
    set outer [info script]
    info script $file
    set cmd {}
    set line 0
    set first 1
    try {
        foreach l [split $text "\n"] {
            incr line
            set parts [split $l ";"]
            set last [expr {[llength $parts] - 1}]
            for {set i 0} {$i <= $last} {incr i} {
                if {[string trim $cmd] eq {}} { set first $line }
                append cmd [lindex $parts $i]
                if {$i < $last} {
                    # A semicolon ends a command only where the command is complete: not inside braces,
                    # quotes or brackets, not escaped, and not in a comment (which runs to the end of the line).
                    if {[string index [string trimleft $cmd] 0] ne "#" && [::lvd::scriptCommandEnds $cmd]} {
                        ::lvd::runScriptCommand $file $first $cmd
                        set cmd {}
                    } else {
                        append cmd ";"
                    }
                    continue
                }
                append cmd "\n"
                if {[::lvd::scriptCommandEnds $cmd]} {
                    ::lvd::runScriptCommand $file $first $cmd
                    set cmd {}
                }
            }
        }
        if {[string trim $cmd] ne {}} {
            return -code error "missing close-brace or quote in the command at line $first of $file"
        }
    } on return {} {
        # `return` in the script ends it, as with source
    } finally {
        info script $outer
    }
}

# True when text is one or more complete commands; info complete also takes a trailing backslash (an
# escaped semicolon or newline) for the end, which continues the command instead.
proc ::lvd::scriptCommandEnds {text} {
    set body [string trimright $text "\n"]
    if {[regexp {(\\+)$} $body -> bs] && [string length $bs] % 2} { return 0 }
    return [info complete $text]
}

# Runs one command of a do script at the global level and prints a non-empty result. An error names the
# script and the line; return ends the script, break and continue outside a loop are errors (as with source).
proc ::lvd::runScriptCommand {file line cmd} {
    set code [catch {uplevel #0 $cmd} result options]
    switch -- $code {
        0 { if {$result ne {}} { puts $result } }
        1 {
            return -code error -errorcode [dict get $options -errorcode] \
                -errorinfo "[dict get $options -errorinfo]\n    (file \"$file\" line $line)" $result
        }
        2 { return -code return }
        3 { return -code error "invoked \"break\" outside of a loop (file \"$file\" line $line)" }
        4 { return -code error "invoked \"continue\" outside of a loop (file \"$file\" line $line)" }
        default { return -code $code $result }
    }
}

proc do {file args} {
    set ::argv $args
    ::lvd::runScript $file
}

# --- not available yet -------------------------------------------------------------------------
proc when {args} { ::lvd::unsupported when "needs kernel support" }
proc bp {args} { ::lvd::unsupported bp "needs kernel support" }
# step and breakpoint are native commands of the fork (patch 0005)
proc vcd {args} { ::lvd::unsupported vcd "use vsim -wlf or -lvdwave to record a waveform" }
