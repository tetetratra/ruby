require_relative 'parse_patern_lang.rb'

last_time = nil
File.open("result.txt", 'w') {}
File.open("argument.txt", 'w') {}

loop do
  File.open("argument.txt", 'r') do |fa|
    time    = fa.gets.chomp
    string  = fa.gets&.chomp
    pattern = fa.gets&.chomp
    next if last_time == time
    p time
    p string
    p pattern

    last_time = time

    File.open("result.txt", 'w') do |fr|
      if pattern.nil? || pattern.empty?
        fr.puts
      else
        result = run(string, pattern)
        p result
        fr.puts result
      end
    end
    puts '---'
  end
  sleep 0.1
end


