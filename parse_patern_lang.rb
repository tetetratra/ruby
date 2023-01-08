TCL_MAX = 100

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

BackRef = Struct.new(:body) do
  def inspect
    "\\#{body}"
  end
end

class Range
  def shift(n)
    (self.begin + n)..(self.end + n)
  end
end

def run(string_raw, pattern_exp)
  string = string_raw.split('->')

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
  include_log = command.include?('_') ? '_' : 'x'
  size = s.lines.size
  "#{type} #{save} #{include_log} #{size}" # /foo/d1_ など
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
      pattern_ast = parse(pattern_code)
      p pattern_ast if $debug

      pattern_bytecode = compile(pattern_ast)
      p pattern_bytecode if $debug

      is_last_pattern = patterns.size == i + 1
      scan(pattern_bytecode, string, is_last_pattern).map do |last_vm|
        range = last_vm[:sp_from]..last_vm[:sp]
        new_skips = last_vm[:skips].map { |i| i + indexes.first }
        {
          indexes: range.map { |i| i + indexes.first },
          indexes_for: indexes, # 前回のindexes
          string: string[range],
          skips: [*skips, *new_skips]
        }
      end
    end
  end
end

MACRO = {
  'RECENT' => -> _s {
    [Times.new('.', TCL_MAX/2), Doller.new] # '.{n}$'
  },
  'BEGINNING' => -> _s {
    [Hat.new, Times.new('.', TCL_MAX/2)]
  },
  '[a-z_<>!?.]*~[a-z_<>!?.]*' => -> s {
    from, to = s.split('~')
    from = '.' if from.nil? || from.empty?
    to = '.' if to.nil? || to.empty?
    ["\\#{from}", Mul.new('.'), "\\#{to}"] # '\from.*\to'
  }
}

def parse(pattern_str)
  macro_regex = MACRO.keys.join('|')
  parsed = pattern_str.scan(%r@#{macro_regex}|\\\d|{\d+}|\^|\$|\.|[A-Z]+|(?<= )_|_(?= )|\\?[a-z_<>!?]+|\+/d|\*/d|\+|\*|\(|\)|_|~@)
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
    when '+'
      pattern << Plus.new(pattern.pop)
    when '*'
      pattern << Mul.new(pattern.pop)
    when /{(\d+)}/
      pattern << Times.new(pattern.pop, $1.to_i)
    when '^'
      pattern << Hat.new
    when '$'
      pattern << Doller.new
    when /\\(\d)/
      pattern << BackRef.new($1.to_i)
    else
      if matched_macro = MACRO.find { |(regex, _replace)| Regexp.new(regex).match?(poped) }
        pattern.concat matched_macro[1].call(poped)
      else
        pattern << poped
      end
    end
  end
  pattern
end

def compile(pattern)
  refi = -1 # ref_to 0 は実行中は現れない(matchが先にくるので)

  compile_r = ->(pat) do
    case pat
    when String
      ["char #{pat}"]
    when Array
      refi += 1
      [
        refi.zero? ? nil : "ref_from #{refi}",
        *pat.map { |p| compile_r.(p) }.flatten(1),
        refi.zero? ? nil : "ref_to #{refi}",
      ].compact
    when BackRef
      ["backref #{pat.body}"]
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

def scan(code, string, is_last_pattern)
  from = 0
  arr = []
  loop do
    last_vm = exec(code, string, from, is_last_pattern)
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

def exec(init_codes, string, from, is_last_pattern)
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
    when /^char (.*)/
      method = string[sp]
      p method if $debug
      next frames_tail if method.nil?

      c = $1
      next_skips = skips.dup
      if c.start_with?('\\')
        c = c.sub('\\', '')
        next_skips << sp
      end

      if method == c || c == '.'
        next [*frames_tail, { **f, sp: sp + 1, pc: pc + 1, skips: next_skips }]
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
    when /^ref_from (\d)/
      next [*frames_tail, { **f, pc: pc + 1, "ref_from_#{$1}": sp }]
    when /^ref_to (\d)/
      next [*frames_tail, { **f, pc: pc + 1, "ref_to_#{$1}": sp - 1 }]
    when /^backref (\d)/
      chars = string[f[:"ref_from_#{$1}"]..f[:"ref_to_#{$1}"]]
      # jumpで戻ってくる時の位置の計算が合うように、charの連続ではなく1個のcharsに置き換えている
      next [
        *frames_tail, {
          **f,
          pc: pc,
          codes: [
            *codes[0..(pc-1)],
            "chars #{chars.join(' ')}",
            *codes[(pc+1)..-1]
          ]
        }
      ]
    when /chars (.*)/
      chars = $1.split.map { |c| c.sub('\\', '') }
      methods = string[sp..-1].take(chars.size).compact
      next frames_tail if methods.size < chars.size

      add_skips = []
      methods = methods.map.with_index do |method, i|
        if method.start_with?('\\')
          add_skips << sp + i
          method.sub('\\', '')
        else
          method
        end
      end

      if chars == methods
        next [*frames_tail, { **f, sp: sp + 1, pc: pc + 1, skips: [*skips, *add_skips] }]
      else
        next frames_tail
      end

    when /^match/
      break { **f, sp: sp - 1 }
    end
  end
end

if $PROGRAM_NAME == __FILE__
  $debug = true
  puts run(ARGV[0], ARGV[1])
end
