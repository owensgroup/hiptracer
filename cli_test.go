package cli

import (
    "database/sql"
    "os"
    "testing"
    _ "github.com/mattn/go-sqlite3"
    "gopkg.in/go-rillas/subprocess.v1"
)



func TestNumEvents(t *testing.T) {
    response := subprocess.Run("../examples/vectorAdd/vectoradd_hip.exe")
    if response.ExitCode != 0 {
        t.Fatal(response.ExitCode)
    }

    db, err := sql.Open("sqlite3", *captureName)
    if err != nil {
        t.Fatal(err)
    }
    defer db.Close()

    rows, err := db.Query("SELECT Id, Name FROM Events")
    if err != nil {
        t.Fatal(err)
    }

    var numEvents int = 0
    for rows.Next() {
        var id int
        var name string
        err = rows.Scan(&id, &name)
        if err != nil {
            t.Fatal(err)
        }
        numEvents++
    }
    err = rows.Err()
    if err != nil {
        t.Fatal(err)
    }
    if numEvents != 11 {
        t.Fatal(err)
    }
}

// func TestEventTypes(t *testing.T) {

// }

// func TestEventNames(t *testing.T) {

// }

//func TestMalloc
//
//func TestMemcpy
//
//func TestHostAllocationCapture

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
        setupEnv("", "", false) // Turn off capture
        response := subprocess.Run("./examples/bin/vectoradd_hip.exe")
        _ = response
    }
}

func BenchmarkVectoradd_Capture(b *testing.B) {
    for i := 0; i < b.N; i++ {
        response := subprocess.Run("./examples/bin/vectoradd_hip.exe")
        _ = response
    }
}

func BenchmarkKripke_NoCapture(b *testing.B) {
    for i := 0; i < b.N; i++ {
        setupEnv("", "", false)
        response := subprocess.Run("./examples/bin/kripke")
        _ = response
    }
}

func BenchmarkKripke_Capture(b *testing.B) {
    for i := 0; i < b.N; i++ {
        response := subprocess.Run("./examples/bin/kripke")
        _ = response
    }
}

func TestMain(m *testing.M) {
    setupEnv(*libraryLocation, *captureName, *debug);
    os.Exit(m.Run())
}
