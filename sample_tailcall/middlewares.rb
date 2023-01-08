def send(request, middlewares)
  case middlewares[0]
  when :replace_newline
    replace_newline(request, middlewares[1..-1])
  when :add_time
    add_time(request, middlewares[1..-1])
  when :logging
    logging(request, middlewares[1..-1])
  when :word_count
    word_count(request, middlewares[1..-1])
  else
    receive(request)
  end
end

def replace_newline(request, middlewares)
  converted_request = request.gsub("\n", ' ')
  send(converted_request, middlewares)
end

def add_time(request, middlewares)
  converted_request = request + Time.now.strftime(" (%H:%M)")
  send(converted_request, middlewares)
end

def logging(request, middlewares)
  File.open('log.txt') do |f|
    f.puts '---'
    f.puts 'request size: ' + request.size
    f.puts request
  end
  send(request, middlewares)
end

def word_count(request, middlewares)
  count = wc(request, 1)
  converted_request = request + " (#{count} words)"
  send(converted_request, middlewares)
end

def wc(string, count)
  if string[0].nil?
    count
  elsif string[0] == ' '
    wc(string[1..-1], count + 1)
  else
    wc(string[1..-1], count)
  end
end

def receive(request)
  raise 'unknown error'
end

send("Hello\nWorld Wide Web." * 1000, [:replace_newline, :word_count, :add_time])
