## Copyright (C) 2024 Daniel Marks
##

## Author: Daniel Marks <Daniel Marks@VECTRON>
## Created: 2024-06-07

function y = lpffbk (a, k, n)
  
if nargin<1
  a = 0.1;
endif
if nargin<2
  k = 0.0;
endif
if nargin<3
  n = 1;
endif

y=0;

f=(0:0.01:0.5);
w=f*(2*pi);
z=exp(1i*w);

y = ((1-a).^n)./((1-a.*z).^n-k*(1-a).^n);

m=(0:n-1);

p = a./(1-exp((1i*2*pi/n).*m).*(1-a)*(k^(1/n)));

figure(1);clf;
subplot(3,1,1);
plot(f,10*log10(abs(y)),'-');
subplot(3,1,2);
plot(f,angle(y),'-');
subplot(3,1,3);
plot(real(p),imag(p),'o');

endfunction
