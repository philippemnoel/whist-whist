import { createServer } from "http"
import { Server, Socket } from "socket.io"

// Initialize socket.io server
const httpServer = createServer()
const io = new Server(httpServer, {
  cors: {
    origin: "*",
  },
})

// Listens for client/server events
io.on("connection", (socket: Socket) => {
  socket.emit("connected", io.engine.clientsCount)
  socket.broadcast.emit("connected", io.engine.clientsCount)

  socket.onAny((eventName, ...args) => {
    socket.broadcast.emit(eventName, args)
  })
})

// Listens for connections on port 32261
httpServer.listen(32261)
