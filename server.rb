require_relative 'parse_patern_lang.rb'

last_time = nil
File.open("result.txt", 'w') {}
File.open("argument.txt", 'w') {}

loop do
  File.open("argument.txt", 'r') do |fa|
    puts '---'
    p time    = fa.gets&.chomp
    p string  = fa.gets&.chomp
    p pattern = fa.gets&.chomp
    next if last_time == time

    last_time = time

    File.open("result.txt", 'w') do |fr|
      if pattern.empty?
        fr.puts # 改行文字だけを出力
      else
        fr.puts run(string, pattern)
      end
    end
  end
  sleep 1
  # puts 'sleep'
end


