# A small perlbench-like workload: build fake e-mails, tokenize them, count
# words in hashes, score them with a word list (like a spam filter), and drop
# old mails. Lots of small strings and hash entries -> malloc heavy.
use strict;
use warnings;

my $N = shift // 60000;
srand(42);
my @words = map { join "", map { chr(97 + int(rand(26))) } 1 .. (3 + int(rand(8))) } 1 .. 5000;
my %spam = map { $words[int(rand(@words))] => rand() } 1 .. 800;

my (@inbox, %freq);
my $score_sum = 0;
for my $i (1 .. $N) {
    my $body = join " ", map { $words[int(rand(@words))] } 1 .. (20 + int(rand(80)));
    my %mail = (id => $i, from => "user$i\@example.com", subject => "msg $i", body => $body);
    my $score = 0;
    for my $w (split /\s+/, $body) {
        $freq{$w}++;
        $score += $spam{$w} // 0;
    }
    $mail{score} = $score;
    $score_sum += $score;
    push @inbox, \%mail;
    if ($i % 5000 == 0) {    # every 5000 mails: keep the best-scoring third, forget the rest
        my @s = sort { $a->{score} <=> $b->{score} } @inbox;
        @inbox = @s[0 .. int($#s / 3)];
    }
}
my @top = (sort { $freq{$b} <=> $freq{$a} } keys %freq)[0 .. 4];
printf "mails=%d kept=%d top=%s\n", $N, scalar(@inbox), join(",", @top);
