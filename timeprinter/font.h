#ifndef _FONT_H
#define _FONT_H

#undef F
#define F 1
#define S 0

typedef unsigned char fontcolumn;

#define CHAR1(A1,B1,C1,D1,E1,F1,G1,H1) \
  { 0b ## H1 ## G1 ## F1 ## E1 ## D1 ## C1 ## B1 ## A1 }

#define CHAR2(A1,A2,B1,B2,C1,C2,D1,D2,E1,E2,F1,F2,G1,G2,H1,H2) \
  { 0b ## H1 ## G1 ## F1 ## E1 ## D1 ## C1 ## B1 ## A1 , \
    0b ## H2 ## G2 ## F2 ## E2 ## D2 ## C2 ## B2 ## A2  }

#define CHAR3(A1,A2,A3,B1,B2,B3,C1,C2,C3,D1,D2,D3,E1,E2,E3,F1,F2,F3,G1,G2,G3,H1,H2,H3) \
  { 0b ## H1 ## G1 ## F1 ## E1 ## D1 ## C1 ## B1 ## A1 , \
    0b ## H2 ## G2 ## F2 ## E2 ## D2 ## C2 ## B2 ## A2 , \
    0b ## H3 ## G3 ## F3 ## E3 ## D3 ## C3 ## B3 ## A3 }

#define CHAR4(A1,A2,A3,A4,B1,B2,B3,B4,C1,C2,C3,C4,D1,D2,D3,D4,E1,E2,E3,E4,F1,F2,F3,F4,G1,G2,G3,G4,H1,H2,H3,H4) \
  { 0b ## H1 ## G1 ## F1 ## E1 ## D1 ## C1 ## B1 ## A1 , \
    0b ## H2 ## G2 ## F2 ## E2 ## D2 ## C2 ## B2 ## A2 , \
    0b ## H3 ## G3 ## F3 ## E3 ## D3 ## C3 ## B3 ## A3 , \
    0b ## H4 ## G4 ## F4 ## E4 ## D4 ## C4 ## B4 ## A4 }

#define CHAR5(A1,A2,A3,A4,A5,B1,B2,B3,B4,B5,C1,C2,C3,C4,C5,D1,D2,D3,D4,D5,E1,E2,E3,E4,E5,F1,F2,F3,F4,F5,G1,G2,G3,G4,G5,H1,H2,H3,H4,H5) \
  { 0b ## H1 ## G1 ## F1 ## E1 ## D1 ## C1 ## B1 ## A1 , \
    0b ## H2 ## G2 ## F2 ## E2 ## D2 ## C2 ## B2 ## A2 , \
    0b ## H3 ## G3 ## F3 ## E3 ## D3 ## C3 ## B3 ## A3 , \
    0b ## H4 ## G4 ## F4 ## E4 ## D4 ## C4 ## B4 ## A4 , \
    0b ## H5 ## G5 ## F5 ## E5 ## D5 ## C5 ## B5 ## A5 }

#define CHAR(N,S,...) \
  const unsigned int N ## _char_width = S; \
  const fontcolumn N ## _char[S] = CHAR ## S (__VA_ARGS__);

CHAR(A,3,
  S, F, S, 
  F, S, F, 
  F, S, F, 
  F, F, F, 
  F, S, F, 
  F, S, F, 
  F, S, F, 
  F, S, F 
);

CHAR(a,5,
  S, S, S, S, S,
  S, S, S, S, S,
  S, S, S, S, S,
  S, F, F, F, S, 
  F, S, S, F, S,
  F, S, S, F, S,
  F, S, S, F, S,
  S, F, F, S, F
);

CHAR(B,3,
  F, F, S, 
  F, S, F, 
  F, S, F, 
  F, F, S, 
  F, S, F, 
  F, S, F, 
  F, S, F, 
  F, F, S 
);

CHAR(b,3,
  S, S, S, 
  S, S, S, 
  F, S, S, 
  F, S, S, 
  F, F, S, 
  F, S, F, 
  F, S, F, 
  F, F, S 
);

CHAR(C,3,
  S, F, S, 
  F, S, F, 
  F, S, S, 
  F, S, S, 
  F, S, S, 
  F, S, S, 
  F, S, F, 
  S, F, S 
);

CHAR(c,3,
  S, S, S, 
  S, S, S, 
  S, S, S, 
  S, F, S, 
  F, S, F, 
  F, S, S, 
  F, S, F, 
  S, F, S 
);

CHAR(D,3,
  F, F, S, 
  F, S, F, 
  F, S, F, 
  F, S, F, 
  F, S, F, 
  F, S, F, 
  F, S, F, 
  F, F, S 
);

CHAR(d,3,
  S, S, S, 
  S, S, S, 
  S, S, F, 
  S, S, F, 
  S, F, F, 
  F, S, F, 
  F, S, F, 
  S, F, F 
);

CHAR(E,3,
  F, F, F, 
  F, S, S, 
  F, S, S, 
  F, F, F, 
  F, S, S, 
  F, S, S, 
  F, S, S, 
  F, F, F 
);

CHAR(e,4,
  S, S, S, S,
  S, S, S, S,
  S, S, S, S,
  S, F, F, S, 
  F, S, S, F, 
  F, F, F, F, 
  F, S, S, S,
  S, F, F, F
);

CHAR(F,3,
  F, F, F, 
  F, S, S, 
  F, S, S, 
  F, F, F, 
  F, S, S, 
  F, S, S, 
  F, S, S, 
  F, S, S 
);

CHAR(f,3,
  S, S, S, 
  S, S, S, 
  S, F, S, 
  F, S, F, 
  F, S, S, 
  F, F, S, 
  F, S, S, 
  F, S, S 
);

CHAR(G,4,
  S, F, F, S, 
  F, S, S, F, 
  F, S, S, S,
  F, S, S, S,
  F, S, F, F,
  F, S, S, F, 
  F, S, S, F, 
  S, F, F, S 
);

CHAR(g,4,
  S, S, S, S, 
  S, S, S, S, 
  S, F, F, S,
  F, S, S, F,
  F, S, S, F,
  S, F, F, F, 
  S, S, S, F, 
  F, F, F, S 
);

CHAR(H,3,
  F, S, F, 
  F, S, F, 
  F, S, F, 
  F, F, F, 
  F, F, F, 
  F, S, F, 
  F, S, F, 
  F, S, F 
);

CHAR(h,3,
  S, S, S, 
  S, S, S, 
  F, S, S, 
  F, S, S, 
  F, F, S, 
  F, S, F, 
  F, S, F, 
  F, S, F 
);

CHAR(I,3,
  F, F, F, 
  S, F, S, 
  S, F, S, 
  S, F, S, 
  S, F, S, 
  S, F, S, 
  S, F, S, 
  F, F, F 
);

CHAR(i,1,
  S, 
  S, 
  F, 
  S, 
  F, 
  F, 
  F, 
  F 
);

CHAR(J,3,
  S, S, F, 
  S, S, F, 
  S, S, F, 
  S, S, F, 
  S, S, F, 
  S, S, F, 
  F, S, F, 
  S, F, S 
);

CHAR(j,3,
  S, S, S, 
  S, S, S, 
  S, S, F, 
  S, S, S, 
  S, S, F, 
  S, S, F, 
  F, S, F, 
  S, F, S 
);

CHAR(K,3,
  F, S, F, 
  F, S, F, 
  F, S, F, 
  F, F, S, 
  F, F, S, 
  F, S, F, 
  F, S, F, 
  F, S, F 
);

CHAR(k,3,
  S, S, S, 
  S, S, S, 
  S, S, S, 
  F, S, F, 
  F, S, F, 
  F, F, S, 
  F, S, F, 
  F, S, F 
);

CHAR(L,3,
  F, S, S, 
  F, S, S, 
  F, S, S, 
  F, S, S, 
  F, S, S, 
  F, S, S, 
  F, S, S, 
  F, F, F 
);

CHAR(l,1,
  S,
  S,
  F,
  F,
  F,
  F,
  F,
  F
);

CHAR(M,5,
  S, F, S, F, S, 
  F, S, F, S, F, 
  F, S, F, S, F, 
  F, S, F, S, F, 
  F, S, F, S, F, 
  F, S, F, S, F, 
  F, S, F, S, F, 
  F, S, F, S, F 
);

CHAR(m,5,
  S, S, S, S, S, 
  S, S, S, S, S, 
  S, S, S, S, S, 
  S, F, S, F, S, 
  F, S, F, S, F, 
  F, S, F, S, F, 
  F, S, F, S, F, 
  F, S, F, S, F 
);

CHAR(N,3,
  S, F, S, 
  F, S, F, 
  F, S, F, 
  F, S, F, 
  F, S, F, 
  F, S, F, 
  F, S, F, 
  F, S, F 
);

CHAR(n,3,
  S, S, S, 
  S, S, S, 
  S, S, S, 
  S, F, S, 
  F, S, F, 
  F, S, F, 
  F, S, F, 
  F, S, F 
);

CHAR(O,3,
  S, F, S, 
  F, S, F, 
  F, S, F, 
  F, S, F, 
  F, S, F, 
  F, S, F, 
  F, S, F, 
  S, F, S 
);

CHAR(o,3,
  S, S, S, 
  S, S, S, 
  S, S, S, 
  S, F, S, 
  F, S, F, 
  F, S, F, 
  F, S, F, 
  S, F, S 
);

CHAR(P,3,
  F, F, S, 
  F, S, F, 
  F, S, F, 
  F, S, F, 
  F, F, S, 
  F, S, S, 
  F, S, S, 
  F, S, S 
);

CHAR(p,3,
  S, S, S,
  S, S, S,
  F, F, S,
  F, S, F,
  F, S, F,
  F, F, S,
  F, S, S,
  F, S, S 
);

CHAR(Q,4,
  S, F, F, S, 
  F, S, S, F, 
  F, S, S, F, 
  F, S, S, F, 
  F, S, S, F, 
  F, F, S, F, 
  F, S, F, S, 
  S, F, S, F 
);

CHAR(q,3,
  S, S, S,
  S, S, S,
  S, F, F,
  F, S, F,
  F, S, F,
  S, F, F,
  S, S, F,
  S, S, F 
);

CHAR(R,3,
  F, F, S, 
  F, S, F, 
  F, S, F, 
  F, F, S, 
  F, S, F, 
  F, S, F, 
  F, S, F, 
  F, S, F
);

CHAR(r,3,
  S, S, S, 
  S, S, S, 
  S, S, S, 
  S, F, F, 
  F, S, S, 
  F, S, S, 
  F, S, S, 
  F, S, S
);

CHAR(S,3,
  S, F, F, 
  F, S, S, 
  F, S, S, 
  S, F, S, 
  S, F, S, 
  S, S, F, 
  S, S, F, 
  F, F, S 
);

CHAR(s,3,
  S, S, S,
  S, S, S,
  S, S, S,
  S, F, F, 
  F, S, S, 
  S, F, S, 
  S, S, F, 
  F, F, S 
);

CHAR(T,3,
  F, F, F, 
  S, F, S, 
  S, F, S, 
  S, F, S, 
  S, F, S, 
  S, F, S, 
  S, F, S, 
  S, F, S 
);

CHAR(t,3,
  S, S, S, 
  S, S, S, 
  S, F, S, 
  F, F, F, 
  S, F, S, 
  S, F, S, 
  S, F, S, 
  S, S, F
);

CHAR(U,3,
  F, S, F, 
  F, S, F, 
  F, S, F, 
  F, S, F, 
  F, S, F, 
  F, S, F, 
  F, S, F, 
  S, F, S 
);

CHAR(u,4,
  S, S, S, S, 
  S, S, S, S, 
  S, S, S, S, 
  F, S, S, F, 
  F, S, S, F, 
  F, S, S, F, 
  F, S, S, F, 
  S, F, F, S 
);

CHAR(V,3,
  F, S, F, 
  F, S, F, 
  F, S, F, 
  F, S, F, 
  F, S, F, 
  F, F, F, 
  S, F, S, 
  S, F, S 
);

CHAR(v,3,
  S, S, S, 
  S, S, S, 
  S, S, S, 
  F, S, F, 
  F, S, F, 
  F, S, F, 
  S, F, S, 
  S, F, S 
);

CHAR(W,5,
  F, S, F, S, F, 
  F, S, F, S, F, 
  F, S, F, S, F, 
  F, S, F, S, F, 
  F, S, F, S, F, 
  F, S, F, S, F, 
  F, S, F, S, F, 
  S, F, S, F, S 
);

CHAR(w,5,
  S, S, S, S, S, 
  S, S, S, S, S, 
  S, S, S, S, S, 
  F, S, F, S, F, 
  F, S, F, S, F, 
  F, S, F, S, F, 
  F, S, F, S, F, 
  S, F, S, F, S 
);

CHAR(X,3,
  F, S, F, 
  F, S, F, 
  F, S, F, 
  S, F, S, 
  S, F, S, 
  F, S, F, 
  F, S, F, 
  F, S, F 
);

CHAR(x,3,
  S, S, S, 
  S, S, S, 
  S, S, S, 
  F, S, F, 
  F, S, F, 
  S, F, S, 
  F, S, F, 
  F, S, F 
);

CHAR(Y,3,
  F, S, F, 
  F, S, F, 
  F, S, F, 
  S, F, S, 
  S, F, S, 
  S, F, S, 
  S, F, S, 
  S, F, S 
);

CHAR(y,3,
  S, S, S, 
  S, S, S, 
  F, S, F, 
  F, S, F, 
  S, F, F, 
  S, S, F, 
  F, S, F, 
  S, F, S 
);

CHAR(Z,4,
  F, F, F, F, 
  S, S, S, F,
  S, S, F, S,
  S, S, F, S,
  S, F, S, S,
  S, F, S, S,
  F, S, S, S,
  F, F, F, F
);

CHAR(z,4,
  S, S, S, S,
  S, S, S, S,
  S, S, S, S,
  F, F, F, F, 
  S, S, S, F,
  S, F, F, S,
  F, S, S, S,
  F, F, F, F
);

CHAR(space,3,
  S, S, S, 
  S, S, S, 
  S, S, S, 
  S, S, S, 
  S, S, S, 
  S, S, S, 
  S, S, S, 
  S, S, S 
);

CHAR(dot,2,
  S, S, 
  S, S, 
  S, S, 
  S, S, 
  S, S, 
  S, S, 
  F, F, 
  F, F 
);

CHAR(bang,2,
  F, F, 
  F, F, 
  F, F, 
  F, F, 
  F, F, 
  S, S, 
  F, F, 
  F, F 
);

CHAR(colon,1,
  S, 
  S,
  F,
  F,
  S,
  F,
  F,
  S
);

CHAR(zero,4,
  S, S, S, S,
  S, F, F, S, 
  F, S, S, F, 
  F, S, S, F, 
  F, S, S, F, 
  F, S, S, F, 
  F, S, S, F, 
  S, F, F, S 
);

CHAR(one,4,
  S, S, S, S,
  S, S, F, S, 
  S, F, F, S, 
  S, S, F, S, 
  S, S, F, S, 
  S, S, F, S, 
  S, S, F, S, 
  S, F, F, F 
);

CHAR(two,4,
  S, S, S, S,
  S, F, F, S, 
  F, S, S, F, 
  S, S, S, F, 
  S, S, F, S, 
  S, F, S, S, 
  F, S, S, S, 
  F, F, F, F 
);

CHAR(three,4,
  S, S, S, S,
  S, F, F, S, 
  F, S, S, F, 
  S, S, S, F, 
  S, F, F, S, 
  S, S, S, F, 
  F, S, S, F, 
  S, F, F, S 
);

CHAR(four,4,
  S, S, S, S,
  F, S, S, S, 
  F, S, S, S, 
  F, S, F, S, 
  F, S, F, S, 
  F, F, F, F, 
  S, S, F, S, 
  S, S, F, S 
);

CHAR(five,4,
  S, S, S, S,
  F, F, F, F, 
  F, S, S, S, 
  F, F, F, S, 
  S, S, S, F, 
  S, S, S, F, 
  F, S, S, F, 
  S, F, F, S 
);

CHAR(six,4,
  S, S, S, S,
  S, F, F, S, 
  F, S, S, F, 
  F, S, S, S, 
  F, F, F, S, 
  F, S, S, F, 
  F, S, S, F, 
  S, F, F, S 
);

CHAR(severn,4,
  S, S, S, S,
  F, F, F, S, 
  S, S, F, S, 
  S, S, F, S, 
  S, F, F, F, 
  S, S, F, S, 
  S, S, F, S, 
  S, S, F, S 
);

CHAR(eight,4,
  S, S, S, S,
  S, F, F, S, 
  F, S, S, F, 
  F, S, S, F, 
  S, F, F, S, 
  F, S, S, F, 
  F, S, S, F, 
  S, F, F, S 
);

CHAR(nine,4,
  S, S, S, S,
  S, F, F, F, 
  F, S, S, F, 
  F, S, S, F, 
  S, F, F, F, 
  S, S, S, F, 
  S, S, S, F, 
  S, S, S, F 
);



const fontcolumn* font[] = {a_char, b_char, c_char, d_char, e_char, f_char, g_char, h_char, i_char, j_char,
                            k_char, l_char, m_char, n_char, o_char, p_char, q_char, r_char, s_char, t_char,
                            u_char, v_char, w_char, x_char, y_char, z_char, 
                            A_char, B_char, C_char, D_char, E_char, F_char, G_char, H_char, I_char, J_char,
                            K_char, L_char, M_char, N_char, O_char, P_char, Q_char, R_char, S_char, T_char,
                            U_char, V_char, W_char, X_char, Y_char, Z_char,
                            zero_char, one_char, two_char, three_char, four_char,
                            five_char, six_char, severn_char, eight_char, nine_char, 
                            space_char, dot_char, bang_char, colon_char};

const unsigned int font_width[] = {a_char_width, b_char_width, c_char_width, d_char_width, e_char_width, f_char_width, g_char_width, h_char_width, i_char_width, j_char_width,
                                   k_char_width, l_char_width, m_char_width, n_char_width, o_char_width, p_char_width, q_char_width, r_char_width, s_char_width, t_char_width,
                                   u_char_width, v_char_width, w_char_width, x_char_width, y_char_width, z_char_width, 
                                   A_char_width, B_char_width, C_char_width, D_char_width, E_char_width, F_char_width, G_char_width, H_char_width, I_char_width, J_char_width,
                                   K_char_width, L_char_width, M_char_width, N_char_width, O_char_width, P_char_width, Q_char_width, R_char_width, S_char_width, T_char_width,
                                   U_char_width, V_char_width, W_char_width, X_char_width, Y_char_width, Z_char_width, 
                                   zero_char_width, one_char_width, two_char_width, three_char_width, four_char_width,
                                   five_char_width, six_char_width, severn_char_width, eight_char_width, nine_char_width,
                                   space_char_width, dot_char_width, bang_char_width, colon_char_width};


const char font_key[] = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', 
                         'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
                         '0', '1', '2', '3', '4' ,'5', '6', '7', '8', '9',
                         ' ', '.', '!', ':'};

#endif

