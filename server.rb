require "socket"

require_relative 'parse_patern_lang.rb'

serv = TCPServer.new('127.0.0.1', 12345)
loop do
  sock = serv.accept # ソケット OPEN （クライアントからの接続待ち）
  received = sock.recv(1000)
  p received
  string, pattern = received.split("\n")
  result = run(string, pattern)
  p result
  sock.write result # クライアントへ文字列返却
  sock.close # ソケット CLOSE
end

