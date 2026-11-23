# Ruby string benchmark (paper section 6.3, Figure 8), rebuilt from the
# paper's description (their script is not public):
#
#   "repeatedly performs a sequence of string allocations and deallocations,
#    simulating the effect of accumulating results from an API and
#    periodically filtering some out. It allocates a number of strings of a
#    fixed size, then retains references 25% of the strings while dropping
#    references to the rest. Each iteration the length of the strings is
#    doubled. The test requires only a fixed 128 MB to hold the string contents."
#
# Our reading of "fixed 128 MB": each round allocates 128 MB of strings
# and keeps 25% of them; those survivors stay alive during the NEXT round
# (while strings twice as long are allocated) and are dropped after it.
# So live string data never exceeds 128 + 32 MB, matching the paper's figure
# (all allocators stay below ~300 MB there).
#
# The paper's Ruby 2.5 sent every string longer than 23 bytes to malloc.
# Ruby 3.x keeps strings up to ~600 bytes inside its own GC heap, so we start
# at 1000 bytes: 1000, 2000, 4000, 8000, 16000 = 5 rounds per "cycle". We run
# several cycles ("repeatedly performs a sequence") to get a run time close to
# the paper's ~3 s.
#
# Keeping every 4th string is a *regular* pattern: with a normal (sequential)
# allocator, every span keeps live strings at the same slots, so spans can't
# be meshed. With randomized allocation they can.

BYTES_PER_ROUND = 128 * 1024 * 1024
START_LEN = (ARGV[0] || 1000).to_i
ROUNDS = (ARGV[1] || 5).to_i
CYCLES = (ARGV[2] || 4).to_i

kept = []
CYCLES.times do
  len = START_LEN
  ROUNDS.times do
    count = BYTES_PER_ROUND / len
    batch = Array.new(count) { |i| ("a".ord + (i % 26)).chr * len }  # each string is its own malloc
    # check last round's survivors, then drop them (filter them out)
    bad = kept.count { |s| s.count(s[0]) != s.bytesize }
    abort "corrupted strings: #{bad}" if bad > 0
    kept = []
    batch.each_with_index { |s, i| kept << s if i % 4 == 0 }           # keep 25%
    batch = nil
    GC.start                                                           # the other 75% are freed now
    len *= 2
  end
end
STDERR.puts "kept=#{kept.size} bad=0"
