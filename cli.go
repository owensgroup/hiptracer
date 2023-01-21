package cli

import (
    "fmt"
    "strconv"
    "os"
    "flag"
    "gopkg.in/go-rillas/subprocess.v1"
)

var libraryLocation *string = flag.String("tracer-library", "./libhipcapture.so", "Location of capture library")
var captureName *string = flag.String("capture", "./tracer-default.db", "Name for output capture file")
var debug *bool = flag.Bool("debug", false, "Print additional debugging output")

func setupEnv(libraryLocation string, captureName string, debug bool) {
    os.Setenv("LD_PRELOAD", libraryLocation)
    os.Setenv("HIP_VISIBLE_DEVICES", "2")
    os.Setenv("HIPTRACER_EVENTDB", captureName)
    os.Setenv("HIPTRACER_DEBUG", strconv.FormatBool(debug)) // FIXME
}

func main() {
    flag.Parse()

    setupEnv(*libraryLocation, *captureName, *debug);

    response := subprocess.Run(flag.Args()[0], flag.Args()[1:]...)
    fmt.Println(response.StdOut)
}
