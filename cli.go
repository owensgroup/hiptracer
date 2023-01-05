package main

import (
    "fmt"
    "os"
    "flag"
    "gopkg.in/go-rillas/subprocess.v1"
    tea "github.com/charmbracelet/bubbletea"
)

type model struct {
    // events []string
    // cursor int
    // data ?? byte array? + size?, chunk?
    choices []string
    cursor int
    selected map[int]struct{}
}

func main() {
    libraryLocation := flag.String("tracer-library", "./hip/libhiptracer.so", "Location of capture library")

    captureName := flag.String("capture", "tracer-default.sqlite", "Name for output capture file")
    debug := flag.Bool("debug", false, "Print additional debugging output")

    flag.Parse()

    fmt.Println("tail:", flag.Args())

    os.Setenv("LD_PRELOAD", libraryLocation)
    os.Setenv("HIP_VISIBLE_DEVICES", "2") // FIXME

    response := subprocess.Run(flag.Args()[0], flag.Args()[1:]...)
    fmt.Println(response)
}

