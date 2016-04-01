#!/usr/bin/perl
#
# stdev - figure N, min, max, median, mode, mean, & std deviation
#
# pull out all the real numbers in the input
# stream and run standard calculations on them.
# they may be intermixed with other test, need
# not be on the same or different lines, and
# can be in scientific notion (avagadro=6.02e23).
# they also admit a leading + or -.
#
# Tom Christiansen
# tchrist@perl.com

use strict;
use warnings;

use List::Util qw< min max >;

#
my $number_rx = qr{

  # leading sign, positive or negative
    (?: [+-] ? )

  # mantissa
    (?= [0123456789.] )
    (?:
        # "N" or "N." or "N.N"
        (?:
            (?: [0123456789] +     )
            (?:
                (?: [.] )
                (?: [0123456789] * )
            ) ?
      |
        # ".N", no leading digits
            (?:
                (?: [.] )
                (?: [0123456789] + )
            )
        )
    )

  # abscissa
    (?:
        (?: [Ee] )
        (?:
            (?: [+-] ? )
            (?: [0123456789] + )
        )
        |
    )
}x;

my $n = 0;
my $sum = 0;
my @values = ();

my %seen = ();

while (<>) {
	while (/($number_rx)/g) {
		$n++;
		my $num = 0 + $1;  # 0+ is so numbers in alternate form count as same
		$sum += $num;
		push @values, $num;
		$seen{$num}++;
	}
}

die "no values" if $n == 0;

my $mean = $sum / $n;

my $sqsum = 0;
for (@values) {
	$sqsum += ( $_ ** 2 );
}
$sqsum /= $n;
$sqsum -= ( $mean ** 2 );
my $stdev = sqrt($sqsum);

my $max_seen_count = max values %seen;
my @modes = grep { $seen{$_} == $max_seen_count } keys %seen;

my $mode = @modes == 1
	? $modes[0]
	: "(" . join(", ", @modes) . ")";
$mode .= ' @ ' . $max_seen_count;

my $median;
my $mid = int @values/2;
if (@values % 2) {
	$median = $values[ $mid ];
} else {
	$median = ($values[$mid-1] + $values[$mid])/2;
}

my $min = min @values;
my $max = max @values;

printf "n is %d, min is %g, max is %d\n", $n, $min, $max;
printf "median is %g, mean is %g, stdev is %g\n",
	$median, $mean, $stdev;
