## Copyright (C) 2024 Daniel Marks
##

## Author: Daniel Marks <Daniel Marks@VECTRON>
## Created: 2024-06-05

function y = lpfl (b)

f=(0:0.01:0.5);
w=2*pi*f;

lbw = 1-b*cos(w);
lb = 1-b;
% a=((1-
b*cos(w))-sqrt((1-b*cos(w)).^2-(1-b).^2))./(1-b);
a = (lbw-sqrt(lbw.*lbw-lb.*lb))./lb;

figure(1);clf;
subplot(2,1,1);
plot(f,a);
subplot(2,1,2);
plot(f,lbw);

endfunction
