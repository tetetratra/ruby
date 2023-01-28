TCL_MAX = 100

class Call
  attr_accessor :name, :skip, :call_type

  def initialize(name)
    @name = name.gsub(/^\\/, '').gsub(/-$/, '').gsub(/=$/, '')
    @skip = name.start_with?('\\')
    @call_type = case name
                 when /-$/ then :normal_call
                 when /=$/ then :tailcall
                 else            :all_call
                 end
  end

  def inspect
    (skip ? '\\' : '') +
      name +
      case call_type
      when :normal_call then '->'
      when :tailcall then '=>'
      when :all_call then ''
      end
  end
end

Plus = Struct.new(:body) do
  def inspect
    "#{body.inspect}+"
  end
end

Mul = Struct.new(:body) do
  def inspect
    "#{body.inspect}*"
  end
end

MulShort = Struct.new(:body) do
  def inspect
    "#{body.inspect}*?"
  end
end

Times = Struct.new(:body, :times) do
  def inspect
    "#{body.inspect}{#{times}}"
  end
end

Hat = Struct.new(:_) do
  def inspect
    '^'
  end
end

Doller = Struct.new(:_) do
  def inspect
    '$'
  end
end

class Range
  def shift(n)
    (self.begin + n)..(self.end + n)
  end
end

def run(string_raw, pattern_exp)
  string = string_raw.split(/(?<==>)|(?<=->)/)

  p string if $debug
  _, *patterns, command = pattern_exp.split('/')
  filtered = filter(string, patterns)

  case command[0] # 1文字目
  when 'd'
    s = filtered.flat_map do |f|
      indexes, skips = f.fetch_values(:indexes, :skips)
      indexes - skips
    end.join("\n")
  when 'k'
    all = (0..(string.size - 1)).to_a
    s = filtered.map do |f|
      indexes, indexes_for, skips = f.fetch_values(:indexes, :indexes_for, :skips)
      indexes_for - indexes - skips
    end.reduce(all, :&).join("\n")
  when 't'
    s = filtered.flat_map do |f|
      indexes, skips = f.fetch_values(:indexes, :skips)
      indexes - skips
    end.join("\n")
  end
  header = make_header(command, s)
  "#{header}\n#{s}"
rescue => e
  STDERR.puts e.full_message
  ''
end

def make_header(command, s)
  type = command[0]
  save = command.include?('1') ? '1' : 's'
  size = s.lines.size
  "#{type} #{save} #{size}" # /foo/d1_ など
end

def filter(init_string, patterns)
  init_indexes = (0..(init_string.size - 1)).to_a
  init = {
    indexes: init_indexes,
    string: init_string,
    skips: []
  }
  patterns.each_with_index.reduce([init]) do |memo, (pattern_code, i)|
    memo.flat_map do |m|
      indexes, string, skips = m.fetch_values(:indexes, :string, :skips)
      pattern_ast = parse(pattern_code, string)
      p pattern_ast if $debug

      pattern_bytecode = compile(pattern_ast)
      p pattern_bytecode if $debug

      last_vms = scan(pattern_bytecode, string)
      last_vms.map do |last_vm|
        range = last_vm[:sp_from]..last_vm[:sp]
        new_skips = last_vm[:skips].map { |i| i + indexes.first }
        new_includes = last_vm[:includes].map { |i| i + indexes.first }
        {
          indexes: range.map { |i| i + indexes.first },
          indexes_for: indexes, # 前回のindexes
          string: string[range],
          skips: [*(skips - new_includes), *new_skips]
        }
      end
    end
  end
end

def parse(pattern_str, string)
  regex = <<~'REGEX'.lines.map(&:chomp).join('|').yield_self { |s| Regexp.new(s) }
    (RECENT)
    (~)
    (?<= )(_)
    (_)(?= )
    (\\)?(\.)(=|-)?
    (\\)?([a-z0-9_!?\-]+)(=|-)?
    (\+)
    (\*)
    (_)
  REGEX
  parsed = pattern_str.scan(regex).map(&:join)
  range_pattern_flag = false
  pattern = []
  until parsed.empty?
    poped = parsed.shift
    case poped
    when ')'
      i = pattern.rindex('(')
      after_bracket = pattern[(i+1)..nil]
      pattern = if i.zero?
                  [after_bracket]
                else
                  [*pattern[0..(i-1)], after_bracket]
                end
    when 'RECENT'
      count = 0
      string_recent = string.reverse.take_while { |s| count += 1 if s.end_with?('=>'); count <= TCL_MAX / 2 }.reverse
      string_recent.each do |s|
        pattern << Call.new(
          (s.end_with?('=>') ? '' : '\\') +
          s.gsub(/>$/, '')
        )
      end
      pattern << Doller.new
    when '~'
      poped = pattern.pop
      poped.skip = true
      pattern << poped

      pattern << MulShort.new(Call.new('.'))

      range_pattern_flag = true
    when '+'
      pattern << Plus.new(pattern.pop)
    when '*'
      pattern << Mul.new(pattern.pop)
    when '^'
      pattern << Hat.new
    when '$'
      pattern << Doller.new
    else
      call = Call.new(poped)
      call.skip = true if range_pattern_flag
      pattern << call
    end
  end
  pattern
end

def compile(pattern)
  compile_r = ->(pat) do
    case pat
    when Call
      ["char #{pat.name} #{pat.skip} #{pat.call_type}"]
    when Array
      pat.map { |p| compile_r.(p) }.flatten(1)
    when Plus
      compiled = compile_r.(pat.body)
      [
        *compiled,
        "split 1 #{compiled.size + 2}",
        *compiled,
        "jump #{-compiled.size - 1}"
      ]
    when Mul
      compiled = compile_r.(pat.body)
      [
        "split 1 #{compiled.size + 2}",
        *compiled,
        "jump #{-compiled.size - 1}"
      ]
    when MulShort # 最短一致
      compiled = compile_r.(pat.body)
      [
        "split #{compiled.size + 2} 1",
        *compiled,
        "jump #{-compiled.size - 1}"
      ]
    when Times
      compile_r.(pat.body) * pat.times
    when Hat
      ['hat']
    when Doller
      ['doller']
    end
  end
  [*compile_r.(pattern), 'match']
end

def scan(code, string)
  from = 0
  arr = []
  loop do
    last_vm = exec(code, string, from)
    break if last_vm.nil?

    range = last_vm[:sp_from]..last_vm[:sp]
    arr << if range.begin > range.end
             from = last_vm[:sp_from] + 1
             STDERR.puts 'maybe bug'
             nil
           else
             from = last_vm[:sp] + 1
             last_vm
           end
  end
  arr.compact
end

def exec(init_codes, string, from)
  # sp: 0,1,2... から始めることで、前方部分一致をサポート
  init_frames = (from..(string.size - 1)).map do |i|
    { codes: init_codes, sp: i, pc: 0, sp_from: i, skips: [] }
  end.reverse

  loop.reduce(init_frames) do |frames, _|
    break nil if frames.empty?

    f = frames.last
    frames_tail = frames[0..-2]

    sp      = f[:sp] # sp: string pointer
    pc      = f[:pc]
    sp_from = f[:sp_from]
    skips   = f[:skips]
    codes   = f[:codes]
    code    = codes[pc]

    if $debug
      puts
      p frames, code
    end

    case code
    when r = /^char (.*) (.*) (.*)/
      m = code.match(r)
      pattern_method    = m[1].gsub('-', ' ')
      pattern_call_type = m[3].to_sym

      bt_method = string[sp]
      p bt_method if $debug
      next frames_tail if bt_method.nil?

      (
        bt_method_raw,
        bt_method_call_type
      ) = case bt_method
          when /->$/
            [bt_method.sub(/->$/, ''), :normal_call]
          when /=>$/
            [bt_method.sub(/=>$/, ''), :tailcall]
          else # current call の場合
            [bt_method, :normal_call]
          end

      call_type_match = case [pattern_call_type, bt_method_call_type]
                        when [:normal_call, :normal_call] then true
                        when [:normal_call, :tailcall]    then false
                        when [:tailcall,    :normal_call] then false
                        when [:tailcall,    :tailcall]    then true
                        when [:all_call,    :normal_call] then true
                        when [:all_call,    :tailcall]    then true
                        end

      skip = (m[2] == 'true') ||
             (pattern_method == '.' && bt_method_raw == '_')
      next_skips = [*skips, skip ? sp : nil].compact
      includes = (pattern_method == '_' && bt_method_raw == '_') ? [sp] : []
      if (bt_method_raw == pattern_method || pattern_method == '.') && call_type_match
        next [*frames_tail, { **f, sp: sp + 1, pc: pc + 1, skips: next_skips, includes: includes }]
      else
        next frames_tail
      end
    when /^hat/
      if sp == 0
        next [*frames_tail, { **f, pc: pc + 1 }]
      else
        next frames_tail
      end
    when /^doller/
      if sp == string.size
        next [*frames_tail, { **f, pc: pc + 1 }]
      else
        next frames_tail
      end
    when /^split (-?\d+) (-?\d+)/
      j1 = $1.to_i
      j2 = $2.to_i
      next [
        *frames_tail,
        { **f, pc: pc + 1  },
        { **f, pc: pc + j2 },
        { **f, pc: pc + j1 } # 最長一致のため、繰り返す方(j1)を先に試す
      ]
    when /^jump (-?\d+)/
      j = $1.to_i
      next [*frames_tail, { **f, pc: pc + j }]
    when /^match/
      break { **f, sp: sp - 1 }
    end
  end
end

if $PROGRAM_NAME == __FILE__
  $debug = true
  puts run(ARGV[0], ARGV[1])
end
