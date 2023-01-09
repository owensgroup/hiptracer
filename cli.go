package main

import (
    "fmt"
    "strconv"
    "os"
    "flag"
    "gopkg.in/go-rillas/subprocess.v1"
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
    libraryLocation := flag.String("tracer-library", "./libhipcapture.so", "Location of capture library")

    captureName := flag.String("capture", "./tracer-default.db", "Name for output capture file")
    debug := flag.Bool("debug", false, "Print additional debugging output")

    flag.Parse()

    os.Setenv("LD_PRELOAD", *libraryLocation)
    fmt.Println(*libraryLocation);
    os.Setenv("HIP_VISIBLE_DEVICES", "2")
    os.Setenv("HIPTRACER_EVENTDB", *captureName)
    os.Setenv("HIPTRACER_DEBUG", strconv.FormatBool(*debug))

    response := subprocess.Run(flag.Args()[0], flag.Args()[1:]...)
    fmt.Println(response.StdOut)
}

