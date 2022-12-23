require_relative 'parse_patern_lang.rb'
# $debug = true

last_time = nil
File.open("result.txt", 'w') {}
File.open("argument.txt", 'w') {}

loop do
  File.open("argument.txt", 'r') do |fa|
    time    = fa.gets&.chomp
    string  = fa.gets&.chomp
    pattern = fa.gets&.chomp
    next if last_time == time
    p string if $debug
    p pattern if $debug

    last_time = time

    File.open("result.txt", 'w') do |fr|
      if pattern.empty?
        fr.puts # 改行文字だけを出力
      else
        result = run(string, pattern)
        p result if $debug
        fr.puts result
      end
    end
  end
  sleep 1
  # puts 'sleep'
end


