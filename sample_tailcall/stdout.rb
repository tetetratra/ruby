def set_stdout(string)
  write(STDOUT, string)
end

def set_stderr(string)
  write(STDERR, string)
end

def write(fd, cmds)
  case cmds[0]
  when 'date' then fd.write Time.now.strftime("%Y/%m/%d\n")
  when 'time' then fd.write Time.now.strftime("%H:%M\n")
  when 'host' then fd.write `hostname`
  when nil then close(fd)
  end
end

def close()

end
