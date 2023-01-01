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
    libraryLocation := "hip/libhiptracer.so"

    listPoints := flag.Bool("list-points", false, "List instrumentation points")
    tool := flag.String("tool", "none", "Tool to use when rewriting a GPU binary (none, memtrace, inst-count)")
    //instBefore := flag.String("inst-before", "", "Comma separated list of points to instrument _before_ an instruction executes (ex: --inst-before=4,5,11")
    //instAfter := flag.String("inst-after", "", "Comma separated list of points to instrument _after_ an instruction executes (ex: --inst-after=4,5,11)")
    deviceFunc := flag.String("device-function", "", "Device function to be called before / after instrumentation point")

    flag.Parse()

    //response := subprocess.Run("ls", "-l")
    //fmt.Println(response)

    fmt.Println("--ARGS--")
    fmt.Println("list-points:", *listPoints)
    //fmt.Println("inst-before:", *instBefore)
    //fmt.Println("inst-after:", *instAfter)
    fmt.Println("device-function:", *deviceFunc)
    fmt.Println("tool:", *tool)
    fmt.Println("tail:", flag.Args())

    os.Setenv("LD_PRELOAD", libraryLocation)
    os.Setenv("HIP_VISIBLE_DEVICES", "2")
    fmt.Println()
    fmt.Println("--ENV--")
    fmt.Println("LD_PRELOAD:", os.Getenv("LD_PRELOAD"))
    fmt.Println("CWD:", os.Getenv("PWD"))

    response := subprocess.Run(flag.Args()[0], flag.Args()[1:]...)
    fmt.Println(response)

    if (*tool == "memtrace") {
        objdump := subprocess.Run("llvm-objdump", "-d", "./memtrace-gfx908.code")
        fmt.Println(objdump)
    } else if (*tool == "capture") {
        connection, err = := net.Dial("tcp", "127.0.0.1:65266")
    } else {
        objdump := subprocess.Run("llvm-objdump", "-d", "./gfx908.code")
        fmt.Println(objdump)
    }
}

