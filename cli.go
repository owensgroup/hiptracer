package main

import (
    "fmt"
    "strconv"
    "os"
    "flag"
    "gopkg.in/go-rillas/subprocess.v1"
)

libraryLocation := flag.String("tracer-library", "./libhipcapture.so", "Location of capture library")
captureName := flag.String("capture", "./tracer-default.db", "Name for output capture file")
debug := flag.Bool("debug", false, "Print additional debugging output")

func setupEnv(string libraryLocation, string captureName, bool debug) {
    os.Setenv("LD_PRELOAD", libraryLocation)
    os.Setenv("HIP_VISIBLE_DEVICES", "2")
    os.Setenv("HIPTRACER_EVENTDB", captureName)
    os.Setenv("HIPTRACER_DEBUG", strconv.FormatBool(debug)) // FIXME
}

func main() {
    flag.Parse()

    setupEnv(*libraryLocation, *capturnName, *debug);

    response := subprocess.Run(flag.Args()[0], flag.Args()[1:]...)
    fmt.Println(response.StdOut)
}

func TestNumEvents(t *testing.T) {
    response := subprocess.Run("./examples/bin/vectoradd_hip.exe")
    db, err := sql.Open("sqlite3", *captureName)
    if err != nil {
        t.Fatal(err)
    }
    defer db.Close()

    rows, err := db.Query("SELECT Id, EventType, Name FROM Events")
    if err != nil {
        t.Fatal(err)
    }

    numEvents := len(rows)
    if numEvents != 11 {
        t.Fatal(err)
    }
}

// func TestEventTypes(t *testing.T) {

// }

// func TestEventNames(t *testing.T) {

// }

func TestMalloc

func TestMemcpy

func TestHostAllocationCapture

// func TestVectoradd(t *testing.T) {

// }

// func TestAdd4(t *testing.T) {

// }

// func TestMiniNbody(t *testing.T) {

// }

// func TestRtm8(t *testing T) {

// }

// func TestGPUStream(t *testing.T) {

// }

// func TestCudaStream(t *testing.T) {

// }

// func TestGPUBurn(t *testing.T) {

// }

// func TestKripke(t *testing.T) {

// }

func BenchmarkVectoradd_NoCapture(b *testing.B) {
    for i := 0; i < b.N; i++ {
        setupEnv("", "", "") // Turn off capture
        response := subprocess.Run("./examples/bin/vectoradd_hip.exe")
    }
}

func BenchmarkVectoradd_Capture(b *testing.B) {
    for i := 0; i < b.N; i++ {
        response := subprocess.Run("./examples/bin/vectoradd_hip.exe")
    }
}

func BenchmarkKripke_NoCapture(b *testing.B) {
    for i := 0; i < b.N; i++ {
        setupEnv("", "", "")
        response := subprocess.Run("./examples/bin/kripke")
    }
}

func BenchmarkKripke_NoCapture(b *testing.B) {
    for i := 0; i < b.N; i++ {
        response := subprocess.Run("./examples/bin/kripke")
    }
}

func TestMain(m *testing.M) {
    setupEnv(*libraryLocation, *captureName, *debug);
    os.Exit(m.Run())
}