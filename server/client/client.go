package main
import (
	"net"
	"fmt"
)

func main(){
 addr, err := net.ResolveUDPAddr("udp", "localhost:5050")
    if err != nil {
        fmt.Errorf("Couldn't resolve address:", err)
        return
    }
	ln, err := net.DialUDP("udp",nil,addr);
	if err != nil {
		fmt.Println(err)
		return
	}
	defer ln.Close();
	s := "hello";
	buff := []byte(s)
	ln.Write(buff);
	fmt.Println(ln.LocalAddr())
}

