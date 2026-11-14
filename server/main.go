package main

import (
	"encoding/json"
	"fmt"
	"log"
	"math"
	"net"
	"sync"
	"time"
)

// ── Constants ─────────────────────────────────────────────────────────────────

const (
	WindowX      = 800.0
	WindowY      = 600.0
	PaddleWidth  = 15.0
	PaddleHeight = 100.0
	BallRadius   = 12.0

	DefaultBallSpeed   = 300.0
	DefaultPaddleSpeed = 600.0

	TickRate  = 60 // Hz
	WinScore  = 5
)

// Paddle direction values stored on each Client.
const (
	DirUp   = -1
	DirStop = 0
	DirDown = 1
)

// ── Paddle ────────────────────────────────────────────────────────────────────

type Paddle struct {
	X, Y     float64
	InitialX float64
	InitialY float64
	Speed    float64
}

func NewPaddle(x, y, speed float64) *Paddle {
	return &Paddle{X: x, Y: y, InitialX: x, InitialY: y, Speed: speed}
}

// MoveUp moves the paddle upward, clamped to the top wall.
func (p *Paddle) MoveUp(dt float64) {
	delta := p.Speed * dt
	if p.Y-delta >= 0 {
		p.Y -= delta
	} else {
		p.Y = 0
	}
}

// MoveDown moves the paddle downward, clamped to the bottom wall.
func (p *Paddle) MoveDown(dt float64) {
	delta := p.Speed * dt
	if p.Y+delta+PaddleHeight <= WindowY {
		p.Y += delta
	} else {
		p.Y = WindowY - PaddleHeight
	}
}

func (p *Paddle) Reset() {
	p.X = p.InitialX
	p.Y = p.InitialY
}

func (p *Paddle) SetSpeed(s float64) {
	p.Speed = s
}

// ── Ball ──────────────────────────────────────────────────────────────────────

type Ball struct {
	X, Y     float64
	VX, VY   float64
	Speed    float64
	InitialX float64
	InitialY float64
}

func NewBall(x, y, speed float64) *Ball {
	angle := math.Pi / 4.0
	return &Ball{
		X:        x,
		Y:        y,
		VX:       speed * math.Cos(angle),
		VY:       speed * math.Sin(angle),
		Speed:    speed,
		InitialX: x,
		InitialY: y,
	}
}

// Update moves the ball one tick. Returns true if the ball bounced off a wall.
func (b *Ball) Update(dt float64) bool {
	nx := b.X + b.VX*dt
	ny := b.Y + b.VY*dt

	bounced := false
	if ny+2*BallRadius >= WindowY || ny <= 0 {
		b.VY = -b.VY
		bounced = true
	}
	// Clamp within vertical bounds.
	if ny < 0 {
		ny = 0
	} else if ny > WindowY-2*BallRadius {
		ny = WindowY - 2*BallRadius
	}

	b.X = nx
	b.Y = ny
	return bounced
}

// SetSpeed rescales the current velocity to the new speed, preserving direction.
func (b *Ball) SetSpeed(s float64) {
	length := math.Sqrt(b.VX*b.VX + b.VY*b.VY)
	if length > 0 {
		b.VX = b.VX / length * s
		b.VY = b.VY / length * s
	}
	b.Speed = s
}

// HandlePaddleCollision reflects the ball off the paddle at (px, py).
// The bounce angle is determined by where on the paddle face the ball hit,
// spanning ±60° from straight horizontal.
func (b *Ball) HandlePaddleCollision(px, py float64) {
	ballCenterY := b.Y + BallRadius
	paddleCenterY := py + PaddleHeight/2.0
	offset := (ballCenterY - paddleCenterY) / (PaddleHeight / 2.0)
	if offset < -1 {
		offset = -1
	} else if offset > 1 {
		offset = 1
	}
	angle := offset * 60.0 * (math.Pi / 180.0)

	movingRight := b.VX > 0
	dir := 1.0
	if movingRight {
		dir = -1.0
		b.X = px - 2*BallRadius // push ball clear of the left face
	} else {
		b.X = px + PaddleWidth // push ball clear of the right face
	}

	b.VX = dir * b.Speed * math.Cos(angle)
	b.VY = b.Speed * math.Sin(angle)
}

// Reset returns the ball to its starting position and reverses horizontal
// direction to serve toward the player who just conceded.
func (b *Ball) Reset() {
	b.X = b.InitialX
	b.Y = b.InitialY
	b.VX = -b.VX
}

// ── Network types ─────────────────────────────────────────────────────────────

type Client struct {
	addr      *net.UDPAddr
	id        int
	direction int
	gameId    int
	lastSeen  time.Time
}

type Message struct {
	Typeo  string
	Action string
}

// AssignMessage tells a client which player slot it occupies.
type AssignMessage struct {
	Type string `json:"type"`
	PID  int    `json:"pid"`
}

// GameOverMessage is sent (redundantly, several times) when a game ends.
type GameOverMessage struct {
	Type   string `json:"type"`
	Winner int    `json:"winner"`
}

// StateMessage is broadcast to both clients every tick.
type StateMessage struct {
	Type   string  `json:"type"`
	BallX  float64 `json:"bx"`
	BallY  float64 `json:"by"`
	BallVX float64 `json:"vx"`
	BallVY float64 `json:"vy"`
	P1Y    float64 `json:"p1y"`
	P2Y    float64 `json:"p2y"`
	Score1 int     `json:"s1"`
	Score2 int     `json:"s2"`
}

var mp            map[string]*Client
var gamesmp       map[int]*Game
var waitingClient *Client // client waiting to be paired into a game
var nextGameId    int     // monotonically increasing game ID
var muMap         sync.Mutex

// ── Game ──────────────────────────────────────────────────────────────────────

type Game struct {
	GameId   int
	players  [2]*Client
	score    [2]int
	mu       sync.Mutex
	player1  string
	player2  string
	ball     *Ball
	paddles  [2]*Paddle
	done     chan struct{}
	stopOnce sync.Once
}

func NewGame(id int, p1, p2 *Client) *Game {
	return &Game{
		GameId:  id,
		players: [2]*Client{p1, p2},
		player1: p1.addr.String(),
		player2: p2.addr.String(),
		ball:    NewBall(388, 288, DefaultBallSpeed),
		paddles: [2]*Paddle{
			NewPaddle(30, 250, DefaultPaddleSpeed),
			NewPaddle(730, 250, DefaultPaddleSpeed),
		},
		done: make(chan struct{}),
	}
}

// Stop signals the game loop to exit. Safe to call more than once.
func (g *Game) Stop() {
	g.stopOnce.Do(func() { close(g.done) })
}

// cleanup stops the loop and removes the game and both clients from the maps.
func (g *Game) cleanup() {
	g.Stop()
	muMap.Lock()
	delete(mp, g.player1)
	delete(mp, g.player2)
	delete(gamesmp, g.GameId)
	muMap.Unlock()
	log.Printf("Game %d removed (players %s, %s)", g.GameId, g.player1, g.player2)
}

// setDirection records the current movement intent for the given player (1 or 2).
func (g *Game) setDirection(player int, direction int) {
	g.mu.Lock()
	g.players[player-1].direction = direction
	g.mu.Unlock()
}

// overlaps returns true when the ball's bounding box intersects the paddle.
func overlaps(bx, by, px, py float64) bool {
	return bx < px+PaddleWidth &&
		bx+2*BallRadius > px &&
		by < py+PaddleHeight &&
		by+2*BallRadius > py
}

// Update advances the entire game state by dt seconds, broadcasts the new
// state, and returns true when the win condition is reached.
func (g *Game) Update(dt float64, conn *net.UDPConn) bool {
	g.mu.Lock()

	// Apply player movement inputs to paddles.
	for i, paddle := range g.paddles {
		switch g.players[i].direction {
		case DirUp:
			paddle.MoveUp(dt)
		case DirDown:
			paddle.MoveDown(dt)
		}
	}

	// Ball–paddle collision (checked before moving ball to avoid tunnelling).
	for _, paddle := range g.paddles {
		if overlaps(g.ball.X, g.ball.Y, paddle.X, paddle.Y) {
			g.ball.HandlePaddleCollision(paddle.X, paddle.Y)
		}
	}

	// Move ball and handle wall bounces.
	g.ball.Update(dt)

	// Scoring: ball exits left or right edge.
	if g.ball.X+2*BallRadius < 0 {
		// Left edge — player 2 scores.
		g.score[1]++
		g.ball.Reset()
	} else if g.ball.X > WindowX {
		// Right edge — player 1 scores.
		g.score[0]++
		g.ball.Reset()
	}

	gameOver := g.score[0] >= WinScore || g.score[1] >= WinScore

	snap := StateMessage{
		Type:   "STATE",
		BallX:  g.ball.X,
		BallY:  g.ball.Y,
		BallVX: g.ball.VX,
		BallVY: g.ball.VY,
		P1Y:    g.paddles[0].Y,
		P2Y:    g.paddles[1].Y,
		Score1: g.score[0],
		Score2: g.score[1],
	}
	addrs := [2]*net.UDPAddr{g.players[0].addr, g.players[1].addr}
	g.mu.Unlock()

	broadcast(snap, addrs, conn)
	return gameOver
}

// sendAssign tells addr which player slot (1 or 2) it has been assigned.
func sendAssign(conn *net.UDPConn, addr *net.UDPAddr, pid int) {
	data, err := json.Marshal(AssignMessage{Type: "ASSIGN", PID: pid})
	if err != nil {
		log.Printf("assign marshal error: %v", err)
		return
	}
	if _, err := conn.WriteToUDP(data, addr); err != nil {
		log.Printf("assign send error to %s: %v", addr, err)
	}
}

// broadcastGameOver sends a GAME_OVER packet to both addresses.
func broadcastGameOver(winner int, addrs [2]*net.UDPAddr, conn *net.UDPConn) {
	data, err := json.Marshal(GameOverMessage{Type: "GAME_OVER", Winner: winner})
	if err != nil {
		log.Printf("game over marshal error: %v", err)
		return
	}
	for _, addr := range addrs {
		if addr != nil {
			if _, err := conn.WriteToUDP(data, addr); err != nil {
				log.Printf("game over send error to %s: %v", addr, err)
			}
		}
	}
}

// broadcast serialises snap and sends it to both player addresses.
// It holds no game lock and reads no live Game state.
func broadcast(snap StateMessage, addrs [2]*net.UDPAddr, conn *net.UDPConn) {
	data, err := json.Marshal(snap)
	if err != nil {
		log.Printf("broadcast marshal error: %v", err)
		return
	}
	for _, addr := range addrs {
		if addr != nil {
			if _, err := conn.WriteToUDP(data, addr); err != nil {
				log.Printf("broadcast write error to %s: %v", addr, err)
			}
		}
	}
}

// Start launches the fixed-timestep game loop in a background goroutine.
// The loop exits when the win condition is reached or Stop() is called.
func (g *Game) Start(conn *net.UDPConn) {
	const dt float64 = 1.0 / TickRate
	ticker := time.NewTicker(time.Second / time.Duration(TickRate))
	go func() {
		defer ticker.Stop()
		for {
			select {
			case <-g.done:
				return
			case <-ticker.C:
				if g.Update(dt, conn) {
					// Send GAME_OVER several times before tearing down so
					// clients receive it despite UDP packet loss.
					g.mu.Lock()
					winner := 1
					if g.score[1] > g.score[0] {
						winner = 2
					}
					addrs := [2]*net.UDPAddr{g.players[0].addr, g.players[1].addr}
					g.mu.Unlock()
					for i := 0; i < 3; i++ {
						broadcastGameOver(winner, addrs, conn)
						time.Sleep(50 * time.Millisecond)
					}
					g.cleanup()
					return
				}
			}
		}
	}()
}

// ── Message validation & helpers ──────────────────────────────────────────────

func check(msg Message) bool {
	if msg.Typeo == "JOIN" || msg.Typeo == "QUIT" {
		return true
	}
	if msg.Typeo == "INPUT" {
		return msg.Action == "MOVE_UP" || msg.Action == "MOVE_DOWN" || msg.Action == "STOP"
	}
	return false
}

func handleConn(conn *net.UDPConn, msg Message, client *Client) {
	if !check(msg) {
		if _, err := conn.WriteToUDP([]byte("Invalid message"), client.addr); err != nil {
			log.Printf("Write error: %v", err)
		}
		return
	}
	fmt.Printf("Handling %s and %s\n", msg.Typeo, msg.Action)

	if msg.Typeo == "JOIN" {
		if _, err := conn.WriteToUDP([]byte("ACK: joined"), client.addr); err != nil {
			log.Printf("Write error: %v", err)
		}
		return
	}

	if msg.Typeo == "QUIT" {
		muMap.Lock()
		if client.gameId == 0 {
			if waitingClient == client {
				waitingClient = nil
			}
			delete(mp, client.addr.String())
			muMap.Unlock()
		} else {
			game := gamesmp[client.gameId]
			muMap.Unlock()
			if game != nil {
				game.cleanup()
			}
		}
		log.Printf("Client %s quit", client.addr)
		return
	};

	var dir int
	switch msg.Action {
	case "MOVE_UP":
		dir = DirUp
	case "MOVE_DOWN":
		dir = DirDown
	default:
		dir = DirStop
	}

	muMap.Lock()
	game := gamesmp[client.gameId]
	muMap.Unlock()

	if game != nil {
		game.setDirection(client.id, dir)
	}
}

func serialise(msg Message) []byte {
	data, err := json.Marshal(msg)
	if err != nil {
		log.Printf("Marshal error: %v", err)
		return nil
	}
	return data
}

func deserialise(data []byte) Message {
	var msg Message
	if err := json.Unmarshal(data, &msg); err != nil {
		log.Printf("Unmarshal error: %v", err)
	}
	return msg
}

// ── Entry point ───────────────────────────────────────────────────────────────

func main() {
	mp = make(map[string]*Client)
	gamesmp = make(map[int]*Game)

	addr, err := net.ResolveUDPAddr("udp", ":5050")
	if err != nil {
		fmt.Println("Couldn't resolve address:", err)
		return
	}
	conn, err := net.ListenUDP("udp", addr)
	if err != nil {
		fmt.Println(err)
		return
	}
	defer conn.Close()

	// Heartbeat: evict clients that stop sending for more than 40 seconds.
	go func() {
		const timeout = 40 * time.Second
		tick := time.NewTicker(2 * time.Second)
		defer tick.Stop()
		for range tick.C {
			now := time.Now()
			muMap.Lock()
			var dead []*Game
			for addr, c := range mp {
				if now.Sub(c.lastSeen) > timeout {
					log.Printf("Client %s timed out", c.addr)
					if c.gameId == 0 {
						if waitingClient == c {
							waitingClient = nil
						}
						delete(mp, addr)
					} else if g, ok := gamesmp[c.gameId]; ok {
						dead = append(dead, g)
					}
				}
			}
			muMap.Unlock()
			for _, g := range dead {
				g.cleanup()
			}
		}
	}()

	buff := make([]byte, 2048)
	for {
		n, clientAddr, err := conn.ReadFromUDP(buff)
		fmt.Printf("Received message from %s\n", clientAddr.String())
		if err != nil {
			log.Printf("Read error: %v", err)
			continue
		}

		data := append([]byte(nil), buff[:n]...)
		msg := deserialise(data)

		muMap.Lock()
		client, ok := mp[clientAddr.String()]
		if !ok {
			if msg.Typeo != "JOIN" {
				muMap.Unlock()
				continue
			}
			client = &Client{addr: clientAddr}
			mp[clientAddr.String()] = client

			if waitingClient == nil {
				// No partner yet — park this client in the lobby.
				client.id = 1
				waitingClient = client
				log.Printf("Client %s waiting for a partner", clientAddr)
			} else {
				// Pair the waiting client with the new arrival and start a game.
				p1 := waitingClient
				p2 := client
				p2.id = 2
				waitingClient = nil

				nextGameId++
				game := NewGame(nextGameId, p1, p2)
				gamesmp[nextGameId] = game
				p1.gameId = nextGameId
				p2.gameId = nextGameId
				game.Start(conn)
				// Send ASSIGN 3 times with 50 ms gaps so a single dropped
				// UDP packet doesn't leave a client with the wrong player ID.
				go func(p1addr, p2addr *net.UDPAddr) {
					for i := 0; i < 3; i++ {
						sendAssign(conn, p1addr, 1)
						sendAssign(conn, p2addr, 2)
						if i < 2 {
							time.Sleep(50 * time.Millisecond)
						}
					}
				}(p1.addr, p2.addr)
				log.Printf("Game %d started (%s vs %s)", nextGameId, p1.addr, p2.addr)
			}
		}
		client.lastSeen = time.Now()
		fmt.Printf("Active clients: %d  Active games: %d\n", len(mp), len(gamesmp))

		go handleConn(conn, msg, client)
		muMap.Unlock()
	}
}
