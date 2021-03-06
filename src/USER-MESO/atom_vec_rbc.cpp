/* ----------------------------------------------------------------------
   LAMMPS - Large-scale Atomic/Molecular Massively Parallel Simulator
   http://lammps.sandia.gov, Sandia National Laboratories
   Steve Plimpton, sjplimp@sandia.gov

   Copyright (2003) Sandia Corporation.  Under the terms of Contract
   DE-AC04-94AL85000 with Sandia Corporation, the U.S. Government retains
   certain rights in this software.  This software is distributed under
   the GNU General Public License.

   See the README file in the top-level LAMMPS directory.
------------------------------------------------------------------------- */

#include "stdlib.h"
#include "atom_vec_rbc.h"
#include "atom.h"
#include "comm.h"
#include "domain.h"
#include "modify.h"
#include "fix.h"
#include "memory.h"
#include "error.h"

using namespace LAMMPS_NS;

#define DELTA 10000

/* ---------------------------------------------------------------------- */

AtomVecRBC::AtomVecRBC( LAMMPS * lmp ) : AtomVec( lmp ) {
    molecular = 1;
    bonds_allow = angles_allow = dihedrals_allow = 1;
    mass_type = 1;

    comm_x_only = comm_f_only = 1;
    size_forward = 4;
    size_reverse = 4;
    size_border = 7 + 1;                    // Added image information.  Ansel
    size_velocity = 3;
    size_data_atom = 6;                     // this many columns in data file.
    size_data_vel = 4;
    xcol_data = 4;                          // which column in data file coordinates start.

    atom->molecule_flag = 1;
}

/* ----------------------------------------------------------------------
   grow atom arrays
   n = 0 grows arrays by DELTA
   n > 0 allocates arrays to size n
------------------------------------------------------------------------- */

void AtomVecRBC::grow( int n ) {
    if ( n == 0 ) nmax += DELTA;
    else nmax = n;
    atom->nmax = nmax;
    if ( nmax < 0 || nmax > MAXSMALLINT )
        error->one( FLERR, "Per-processor system is too big" );

    tag = memory->grow( atom->tag, nmax, "atom:tag" );
    type = memory->grow( atom->type, nmax, "atom:type" );
    mask = memory->grow( atom->mask, nmax, "atom:mask" );
    image = memory->grow( atom->image, nmax, "atom:image" );
    x = memory->grow( atom->x, nmax, 3, "atom:x" );
    v = memory->grow( atom->v, nmax, 3, "atom:v" );
    f = memory->grow( atom->f, nmax * comm->nthreads, 3, "atom:f" );

    molecule = memory->grow( atom->molecule, nmax, "atom:molecule" );

    nspecial = memory->grow( atom->nspecial, nmax, 3, "atom:nspecial" );
    special = memory->grow( atom->special, nmax, atom->maxspecial, "atom:special" );

    num_bond = memory->grow( atom->num_bond, nmax, "atom:num_bond" );
    bond_type = memory->grow( atom->bond_type, nmax, atom->bond_per_atom,
                              "atom:bond_type" );
    bond_atom = memory->grow( atom->bond_atom, nmax, atom->bond_per_atom,
                              "atom:bond_atom" );
    bond_r0 = memory->grow( atom->bond_r0, nmax, atom->bond_per_atom,
                            "atom:bond_r0" );

    num_angle = memory->grow( atom->num_angle, nmax, "atom:num_angle" );
    angle_type = memory->grow( atom->angle_type, nmax, atom->angle_per_atom,
                               "atom:angle_type" );
    angle_atom1 = memory->grow( atom->angle_atom1, nmax, atom->angle_per_atom,
                                "atom:angle_atom1" );
    angle_atom2 = memory->grow( atom->angle_atom2, nmax, atom->angle_per_atom,
                                "atom:angle_atom2" );
    angle_atom3 = memory->grow( atom->angle_atom3, nmax, atom->angle_per_atom,
                                "atom:angle_atom3" );
    angle_a0 = memory->grow( atom->angle_a0, nmax, atom->angle_per_atom,
                             "atom:angle_a0" );

    num_dihedral = memory->grow( atom->num_dihedral, nmax, "atom:num_dihedral" );
    dihedral_type = memory->grow( atom->dihedral_type, nmax,
                                  atom->dihedral_per_atom, "atom:dihedral_type" );
    dihedral_atom1 =
        memory->grow( atom->dihedral_atom1, nmax, atom->dihedral_per_atom,
                      "atom:dihedral_atom1" );
    dihedral_atom2 =
        memory->grow( atom->dihedral_atom2, nmax, atom->dihedral_per_atom,
                      "atom:dihedral_atom2" );
    dihedral_atom3 =
        memory->grow( atom->dihedral_atom3, nmax, atom->dihedral_per_atom,
                      "atom:dihedral_atom3" );
    dihedral_atom4 =
        memory->grow( atom->dihedral_atom4, nmax, atom->dihedral_per_atom,
                      "atom:dihedral_atom4" );

    if ( atom->nextra_grow )
        for ( int iextra = 0; iextra < atom->nextra_grow; iextra++ )
            modify->fix[atom->extra_grow[iextra]]->grow_arrays( nmax );
}

/* ----------------------------------------------------------------------
   reset local array ptrs
------------------------------------------------------------------------- */

void AtomVecRBC::grow_reset() {
    tag = atom->tag;
    type = atom->type;
    mask = atom->mask;
    image = atom->image;
    x = atom->x;
    v = atom->v;
    f = atom->f;
    molecule = atom->molecule;
    nspecial = atom->nspecial;
    special = atom->special;
    num_bond = atom->num_bond;
    bond_type = atom->bond_type;
    bond_atom = atom->bond_atom;
    bond_r0 = atom->bond_r0;
    num_angle = atom->num_angle;
    angle_type = atom->angle_type;
    angle_atom1 = atom->angle_atom1;
    angle_atom2 = atom->angle_atom2;
    angle_atom3 = atom->angle_atom3;
    angle_a0 = atom->angle_a0;
    num_dihedral = atom->num_dihedral;
    dihedral_type = atom->dihedral_type;
    dihedral_atom1 = atom->dihedral_atom1;
    dihedral_atom2 = atom->dihedral_atom2;
    dihedral_atom3 = atom->dihedral_atom3;
    dihedral_atom4 = atom->dihedral_atom4;
}

/* ----------------------------------------------------------------------
   copy atom I info to atom J
------------------------------------------------------------------------- */

void AtomVecRBC::copy( int i, int j, int delflag ) {
    int k;

    tag[j] = tag[i];
    type[j] = type[i];
    mask[j] = mask[i];
    image[j] = image[i];
    x[j][0] = x[i][0];
    x[j][1] = x[i][1];
    x[j][2] = x[i][2];
    v[j][0] = v[i][0];
    v[j][1] = v[i][1];
    v[j][2] = v[i][2];

    molecule[j] = molecule[i];

    num_bond[j] = num_bond[i];
    for ( k = 0; k < num_bond[j]; k++ ) {
        bond_type[j][k] = bond_type[i][k];
        bond_atom[j][k] = bond_atom[i][k];
        bond_r0[j][k] = bond_r0[i][k];
    }

    num_angle[j] = num_angle[i];
    for ( k = 0; k < num_angle[j]; k++ ) {
        angle_type[j][k] = angle_type[i][k];
        angle_atom1[j][k] = angle_atom1[i][k];
        angle_atom2[j][k] = angle_atom2[i][k];
        angle_atom3[j][k] = angle_atom3[i][k];
        angle_a0[j][k] = angle_a0[i][k];
    }

    num_dihedral[j] = num_dihedral[i];
    for ( k = 0; k < num_dihedral[j]; k++ ) {
        dihedral_type[j][k] = dihedral_type[i][k];
        dihedral_atom1[j][k] = dihedral_atom1[i][k];
        dihedral_atom2[j][k] = dihedral_atom2[i][k];
        dihedral_atom3[j][k] = dihedral_atom3[i][k];
        dihedral_atom4[j][k] = dihedral_atom4[i][k];
    }

    nspecial[j][0] = nspecial[i][0];
    nspecial[j][1] = nspecial[i][1];
    nspecial[j][2] = nspecial[i][2];
    for ( k = 0; k < nspecial[j][2]; k++ ) special[j][k] = special[i][k];

    if ( atom->nextra_grow )
        for ( int iextra = 0; iextra < atom->nextra_grow; iextra++ )
            modify->fix[atom->extra_grow[iextra]]->copy_arrays( i, j, delflag );
}

/* ---------------------------------------------------------------------- */

int AtomVecRBC::pack_comm( int n, int * list, double * buf,
                           int pbc_flag, int * pbc ) {
    int i, j, m;
    double dx, dy, dz;

    m = 0;
    if ( pbc_flag == 0 ) {
        for ( i = 0; i < n; i++ ) {
            j = list[i];
            buf[m++] = x[j][0];
            buf[m++] = x[j][1];
            buf[m++] = x[j][2];
        }
    } else {
        if ( domain->triclinic == 0 ) {
            dx = pbc[0] * domain->xprd;
            dy = pbc[1] * domain->yprd;
            dz = pbc[2] * domain->zprd;
        } else {
            dx = pbc[0] * domain->xprd + pbc[5] * domain->xy + pbc[4] * domain->xz;
            dy = pbc[1] * domain->yprd + pbc[3] * domain->yz;
            dz = pbc[2] * domain->zprd;
        }
        for ( i = 0; i < n; i++ ) {
            j = list[i];
            buf[m++] = x[j][0] + dx;
            buf[m++] = x[j][1] + dy;
            buf[m++] = x[j][2] + dz;
        }
    }
    return m;
}

/* ---------------------------------------------------------------------- */

int AtomVecRBC::pack_comm_vel( int n, int * list, double * buf,
                               int pbc_flag, int * pbc ) {
    int i, j, m;
    double dx, dy, dz, dvx, dvy, dvz;

    m = 0;
    if ( pbc_flag == 0 ) {
        for ( i = 0; i < n; i++ ) {
            j = list[i];
            buf[m++] = x[j][0];
            buf[m++] = x[j][1];
            buf[m++] = x[j][2];
            buf[m++] = v[j][0];
            buf[m++] = v[j][1];
            buf[m++] = v[j][2];
            *( ( tagint * ) &buf[m++] ) = image[j];
        }
    } else {
        if ( domain->triclinic == 0 ) {
            dx = pbc[0] * domain->xprd;
            dy = pbc[1] * domain->yprd;
            dz = pbc[2] * domain->zprd;
        } else {
            dx = pbc[0] * domain->xprd + pbc[5] * domain->xy + pbc[4] * domain->xz;
            dy = pbc[1] * domain->yprd + pbc[3] * domain->yz;
            dz = pbc[2] * domain->zprd;
        }
        if ( !deform_vremap ) {
            for ( i = 0; i < n; i++ ) {
                j = list[i];
                buf[m++] = x[j][0] + dx;
                buf[m++] = x[j][1] + dy;
                buf[m++] = x[j][2] + dz;
                buf[m++] = v[j][0];
                buf[m++] = v[j][1];
                buf[m++] = v[j][2];

                // Change the image-count of ghost particles such that coord(unwrap(p))==coord(unwrap(ghost))   -Ansel
                // image[j] contains all 3 dimension information, in binary.
                int img = image[j];
                int ximg = (  img            & IMGMASK ) - IMGMAX;
                int yimg = ( (img>>IMGBITS)  & IMGMASK ) - IMGMAX;
                int zimg = ( (img>>IMG2BITS) & IMGMASK ) - IMGMAX;
                tagint newimg = ((tagint) (ximg-pbc[0] + IMGMAX) & IMGMASK) |
                                (((tagint) (yimg-pbc[1] + IMGMAX) & IMGMASK) << IMGBITS) |
                                (((tagint) (zimg-pbc[2] + IMGMAX) & IMGMASK) << IMG2BITS);

                *( ( tagint * ) &buf[m++] ) = newimg;
                /*
                        int ximgt = (  newimg            & IMGMASK ) - IMGMAX;
                        int yimgt = ( (newimg>>IMGBITS)  & IMGMASK ) - IMGMAX;
                        int zimgt = ( (newimg>>IMG2BITS) & IMGMASK ) - IMGMAX;

                        printf("local particle...\n");
                        printf("x[j][] = %g %g %g\n",x[j][0],x[j][1],x[j][2]);
                        printf("pbc = %i %i %i\n",pbc[0],pbc[1],pbc[2]);
                        printf("dx dy dz = %g %g %g\n",dx,dy,dz);

                        printf("\nghost particle...\n");
                        printf("x[j][] + d = %g %g %g\n",x[j][0]+dx, x[j][1]+dy, x[j][2]+dz);
                        printf("ximg yimg zimg = %i %i %i\n",ximg,yimg,zimg);
                        printf("unwarp(x[j][]) = %g %g %g\n",x[j][0]+dx + ximgt*domain->xprd, x[j][1]+dy + yimgt*domain->yprd, x[j][2]+dz + zimgt*domain->zprd);
                        printf("--------------------------\n");
                 */

            }
        } else {
            dvx = pbc[0] * h_rate[0] + pbc[5] * h_rate[5] + pbc[4] * h_rate[4];
            dvy = pbc[1] * h_rate[1] + pbc[3] * h_rate[3];
            dvz = pbc[2] * h_rate[2];
            for ( i = 0; i < n; i++ ) {
                j = list[i];
                buf[m++] = x[j][0] + dx;
                buf[m++] = x[j][1] + dy;
                buf[m++] = x[j][2] + dz;
                if ( mask[i] & deform_groupbit ) {
                    buf[m++] = v[j][0] + dvx;
                    buf[m++] = v[j][1] + dvy;
                    buf[m++] = v[j][2] + dvz;
                } else {
                    buf[m++] = v[j][0];
                    buf[m++] = v[j][1];
                    buf[m++] = v[j][2];
                }

                // Change the image-count of ghost particles such that coord(unwrap(p))==coord(unwrap(ghost))   -Ansel
                // image[j] contains all 3 dimension information, in binary.
                int img = image[j];
                int ximg = (  img            & IMGMASK ) - IMGMAX;
                int yimg = ( (img>>IMGBITS)  & IMGMASK ) - IMGMAX;
                int zimg = ( (img>>IMG2BITS) & IMGMASK ) - IMGMAX;
                tagint newimg = ((tagint) (ximg-pbc[0] + IMGMAX) & IMGMASK) |
                                (((tagint) (yimg-pbc[1] + IMGMAX) & IMGMASK) << IMGBITS) |
                                (((tagint) (zimg-pbc[2] + IMGMAX) & IMGMASK) << IMG2BITS);

                *( ( tagint * ) &buf[m++] ) = newimg;
                /*
                        int ximgt = (  newimg            & IMGMASK ) - IMGMAX;
                        int yimgt = ( (newimg>>IMGBITS)  & IMGMASK ) - IMGMAX;
                        int zimgt = ( (newimg>>IMG2BITS) & IMGMASK ) - IMGMAX;

                        printf("local particle...\n");
                        printf("x[j][] = %g %g %g\n",x[j][0],x[j][1],x[j][2]);
                        printf("pbc = %i %i %i\n",pbc[0],pbc[1],pbc[2]);
                        printf("dx dy dz = %g %g %g\n",dx,dy,dz);

                        printf("\nghost particle...\n");
                        printf("x[j][] + d = %g %g %g\n",x[j][0]+dx, x[j][1]+dy, x[j][2]+dz);
                        printf("ximg yimg zimg = %i %i %i\n",ximg,yimg,zimg);
                        printf("unwarp(x[j][]) = %g %g %g\n",x[j][0]+dx + ximgt*domain->xprd, x[j][1]+dy + yimgt*domain->yprd, x[j][2]+dz + zimgt*domain->zprd);
                        printf("--------------------------\n");
                 */
            }
        }
    }
    return m;
}

/* ---------------------------------------------------------------------- */

void AtomVecRBC::unpack_comm( int n, int first, double * buf ) {
    int i, m, last;

    m = 0;
    last = first + n;
    for ( i = first; i < last; i++ ) {
        x[i][0] = buf[m++];
        x[i][1] = buf[m++];
        x[i][2] = buf[m++];
    }
}

/* ---------------------------------------------------------------------- */

void AtomVecRBC::unpack_comm_vel( int n, int first, double * buf ) {
    int i, m, last;

    m = 0;
    last = first + n;
    for ( i = first; i < last; i++ ) {
        x[i][0] = buf[m++];
        x[i][1] = buf[m++];
        x[i][2] = buf[m++];
        v[i][0] = buf[m++];
        v[i][1] = buf[m++];
        v[i][2] = buf[m++];
        image[i] = *( ( tagint * ) &buf[m++] );                 // over-write ghost particle image[].  Ansel
    }
}

/* ---------------------------------------------------------------------- */

int AtomVecRBC::pack_reverse( int n, int first, double * buf ) {
    int i, m, last;

    m = 0;
    last = first + n;
    for ( i = first; i < last; i++ ) {
        buf[m++] = f[i][0];
        buf[m++] = f[i][1];
        buf[m++] = f[i][2];
    }
    return m;
}

/* ---------------------------------------------------------------------- */

void AtomVecRBC::unpack_reverse( int n, int * list, double * buf ) {
    int i, j, m;

    m = 0;
    for ( i = 0; i < n; i++ ) {
        j = list[i];
        f[j][0] += buf[m++];
        f[j][1] += buf[m++];
        f[j][2] += buf[m++];
    }
}

/* ---------------------------------------------------------------------- */

int AtomVecRBC::pack_border( int n, int * list, double * buf,
                             int pbc_flag, int * pbc ) {
    int i, j, m;
    double dx, dy, dz;

    m = 0;
    if ( pbc_flag == 0 ) {
        for ( i = 0; i < n; i++ ) {
            j = list[i];
            buf[m++] = x[j][0];
            buf[m++] = x[j][1];
            buf[m++] = x[j][2];
            buf[m++] = tag[j];
            buf[m++] = type[j];
            buf[m++] = mask[j];
            buf[m++] = molecule[j];
            *( ( tagint * ) &buf[m++] )  = image[j];
        }
    } else {
        if ( domain->triclinic == 0 ) {
            dx = pbc[0] * domain->xprd;
            dy = pbc[1] * domain->yprd;
            dz = pbc[2] * domain->zprd;
        } else {
            dx = pbc[0];
            dy = pbc[1];
            dz = pbc[2];
        }
        for ( i = 0; i < n; i++ ) {
            j = list[i];
            buf[m++] = x[j][0] + dx;
            buf[m++] = x[j][1] + dy;
            buf[m++] = x[j][2] + dz;
            buf[m++] = tag[j];
            buf[m++] = type[j];
            buf[m++] = mask[j];
            buf[m++] = molecule[j];

            // Ansel
            int img = image[j];
            int ximg = (  img            & IMGMASK ) - IMGMAX;
            int yimg = ( (img>>IMGBITS)  & IMGMASK ) - IMGMAX;
            int zimg = ( (img>>IMG2BITS) & IMGMASK ) - IMGMAX;
            tagint newimg = ((tagint) (ximg-pbc[0] + IMGMAX) & IMGMASK) |
                            (((tagint) (yimg-pbc[1] + IMGMAX) & IMGMASK) << IMGBITS) |
                            (((tagint) (zimg-pbc[2] + IMGMAX) & IMGMASK) << IMG2BITS);

            *( ( tagint * ) &buf[m++] )  = newimg;
            /*
                        int ximgt = (  newimg            & IMGMASK ) - IMGMAX;
                        int yimgt = ( (newimg>>IMGBITS)  & IMGMASK ) - IMGMAX;
                        int zimgt = ( (newimg>>IMG2BITS) & IMGMASK ) - IMGMAX;

                        printf("local particle...\n");
                        printf("x[j][] = %g %g %g\n",x[j][0],x[j][1],x[j][2]);
                        printf("pbc = %i %i %i\n",pbc[0],pbc[1],pbc[2]);
                        printf("dx dy dz = %g %g %g\n",dx,dy,dz);

                        printf("\nghost particle...\n");
                        printf("x[j][] + d = %g %g %g\n",x[j][0]+dx, x[j][1]+dy, x[j][2]+dz);
                        printf("ximg yimg zimg = %i %i %i\n",ximg,yimg,zimg);
                        printf("unwarp(x[j][]) = %g %g %g\n",x[j][0]+dx + ximgt*domain->xprd, x[j][1]+dy + yimgt*domain->yprd, x[j][2]+dz + zimgt*domain->zprd);
                        printf("--------------------------\n");
            */
        }
    }

    if ( atom->nextra_border )
        for ( int iextra = 0; iextra < atom->nextra_border; iextra++ )
            m += modify->fix[atom->extra_border[iextra]]->pack_border( n, list, &buf[m] );

    return m;
}

/* ---------------------------------------------------------------------- */

int AtomVecRBC::pack_border_vel( int n, int * list, double * buf,
                                 int pbc_flag, int * pbc ) {
    int i, j, m;
    double dx, dy, dz, dvx, dvy, dvz;

    m = 0;
    if ( pbc_flag == 0 ) {
        for ( i = 0; i < n; i++ ) {
            j = list[i];
            buf[m++] = x[j][0];
            buf[m++] = x[j][1];
            buf[m++] = x[j][2];
            buf[m++] = tag[j];
            buf[m++] = type[j];
            buf[m++] = mask[j];
            buf[m++] = molecule[j];
            buf[m++] = v[j][0];
            buf[m++] = v[j][1];
            buf[m++] = v[j][2];
            *( ( tagint * ) &buf[m++] )  = image[j];
        }
    } else {
        if ( domain->triclinic == 0 ) {
            dx = pbc[0] * domain->xprd;
            dy = pbc[1] * domain->yprd;
            dz = pbc[2] * domain->zprd;
        } else {
            dx = pbc[0];
            dy = pbc[1];
            dz = pbc[2];
        }
        if ( !deform_vremap ) {
            for ( i = 0; i < n; i++ ) {
                j = list[i];
                buf[m++] = x[j][0] + dx;
                buf[m++] = x[j][1] + dy;
                buf[m++] = x[j][2] + dz;
                buf[m++] = tag[j];
                buf[m++] = type[j];
                buf[m++] = mask[j];
                buf[m++] = molecule[j];
                buf[m++] = v[j][0];
                buf[m++] = v[j][1];
                buf[m++] = v[j][2];

                // Ansel
                int img = image[j];
                int ximg = (  img            & IMGMASK ) - IMGMAX;
                int yimg = ( (img>>IMGBITS)  & IMGMASK ) - IMGMAX;
                int zimg = ( (img>>IMG2BITS) & IMGMASK ) - IMGMAX;
                tagint newimg = ((tagint) (ximg-pbc[0] + IMGMAX) & IMGMASK) |
                                (((tagint) (yimg-pbc[1] + IMGMAX) & IMGMASK) << IMGBITS) |
                                (((tagint) (zimg-pbc[2] + IMGMAX) & IMGMASK) << IMG2BITS);

                *( ( tagint * ) &buf[m++] )  = newimg;
                /*
                    int ximgt = (  newimg            & IMGMASK ) - IMGMAX;
                    int yimgt = ( (newimg>>IMGBITS)  & IMGMASK ) - IMGMAX;
                    int zimgt = ( (newimg>>IMG2BITS) & IMGMASK ) - IMGMAX;

                    printf("local particle...\n");
                    printf("x[j][] = %g %g %g\n",x[j][0],x[j][1],x[j][2]);
                    printf("pbc = %i %i %i\n",pbc[0],pbc[1],pbc[2]);
                    printf("dx dy dz = %g %g %g\n",dx,dy,dz);

                    printf("\nghost particle...\n");
                    printf("x[j][] + d = %g %g %g\n",x[j][0]+dx, x[j][1]+dy, x[j][2]+dz);
                    printf("ximg yimg zimg = %i %i %i\n",ximg,yimg,zimg);
                    printf("unwarp(x[j][]) = %g %g %g\n",x[j][0]+dx + ximgt*domain->xprd, x[j][1]+dy + yimgt*domain->yprd, x[j][2]+dz + zimgt*domain->zprd);
                    printf("--------------------------\n");
                 */
            }
        } else {
            dvx = pbc[0] * h_rate[0] + pbc[5] * h_rate[5] + pbc[4] * h_rate[4];
            dvy = pbc[1] * h_rate[1] + pbc[3] * h_rate[3];
            dvz = pbc[2] * h_rate[2];
            for ( i = 0; i < n; i++ ) {
                j = list[i];
                buf[m++] = x[j][0] + dx;
                buf[m++] = x[j][1] + dy;
                buf[m++] = x[j][2] + dz;
                buf[m++] = tag[j];
                buf[m++] = type[j];
                buf[m++] = mask[j];
                buf[m++] = molecule[j];
                if ( mask[i] & deform_groupbit ) {
                    buf[m++] = v[j][0] + dvx;
                    buf[m++] = v[j][1] + dvy;
                    buf[m++] = v[j][2] + dvz;
                } else {
                    buf[m++] = v[j][0];
                    buf[m++] = v[j][1];
                    buf[m++] = v[j][2];
                }

                // Ansel
                int img = image[j];
                int ximg = (  img            & IMGMASK ) - IMGMAX;
                int yimg = ( (img>>IMGBITS)  & IMGMASK ) - IMGMAX;
                int zimg = ( (img>>IMG2BITS) & IMGMASK ) - IMGMAX;
                tagint newimg = ((tagint) (ximg-pbc[0] + IMGMAX) & IMGMASK) |
                                (((tagint) (yimg-pbc[1] + IMGMAX) & IMGMASK) << IMGBITS) |
                                (((tagint) (zimg-pbc[2] + IMGMAX) & IMGMASK) << IMG2BITS);

                *( ( tagint * ) &buf[m++] )  = newimg;
                /*
                        int ximgt = (  newimg            & IMGMASK ) - IMGMAX;
                        int yimgt = ( (newimg>>IMGBITS)  & IMGMASK ) - IMGMAX;
                        int zimgt = ( (newimg>>IMG2BITS) & IMGMASK ) - IMGMAX;

                        printf("local particle...\n");
                        printf("x[j][] = %g %g %g\n",x[j][0],x[j][1],x[j][2]);
                        printf("pbc = %i %i %i\n",pbc[0],pbc[1],pbc[2]);
                        printf("dx dy dz = %g %g %g\n",dx,dy,dz);

                        printf("\nghost particle...\n");
                        printf("x[j][] + d = %g %g %g\n",x[j][0]+dx, x[j][1]+dy, x[j][2]+dz);
                        printf("ximg yimg zimg = %i %i %i\n",ximg,yimg,zimg);
                        printf("unwarp(x[j][]) = %g %g %g\n",x[j][0]+dx + ximgt*domain->xprd, x[j][1]+dy + yimgt*domain->yprd, x[j][2]+dz + zimgt*domain->zprd);
                        printf("--------------------------\n");
                */
            }
        }
    }

    if ( atom->nextra_border )
        for ( int iextra = 0; iextra < atom->nextra_border; iextra++ )
            m += modify->fix[atom->extra_border[iextra]]->pack_border( n, list, &buf[m] );

    return m;
}

/* ---------------------------------------------------------------------- */

int AtomVecRBC::pack_border_hybrid( int n, int * list, double * buf ) {
    int i, j, m;

    m = 0;
    for ( i = 0; i < n; i++ ) {
        j = list[i];
        buf[m++] = molecule[j];
    }
    return m;
}

/* ---------------------------------------------------------------------- */

void AtomVecRBC::unpack_border( int n, int first, double * buf ) {
    int i, m, last;

    m = 0;
    last = first + n;
    for ( i = first; i < last; i++ ) {
        if ( i == nmax ) grow( 0 );
        x[i][0] = buf[m++];
        x[i][1] = buf[m++];
        x[i][2] = buf[m++];
        tag[i] = static_cast<int> ( buf[m++] );
        type[i] = static_cast<int> ( buf[m++] );
        mask[i] = static_cast<int> ( buf[m++] );
        molecule[i] = static_cast<int> ( buf[m++] );
        image[i] = *( ( tagint * ) &buf[m++] );                             // Ansel
    }

    if ( atom->nextra_border )
        for ( int iextra = 0; iextra < atom->nextra_border; iextra++ )
            m += modify->fix[atom->extra_border[iextra]]->
                 unpack_border( n, first, &buf[m] );
}

/* ---------------------------------------------------------------------- */

void AtomVecRBC::unpack_border_vel( int n, int first, double * buf ) {
    int i, m, last;

    m = 0;
    last = first + n;
    for ( i = first; i < last; i++ ) {
        if ( i == nmax ) grow( 0 );
        x[i][0] = buf[m++];
        x[i][1] = buf[m++];
        x[i][2] = buf[m++];
        tag[i] = static_cast<int> ( buf[m++] );
        type[i] = static_cast<int> ( buf[m++] );
        mask[i] = static_cast<int> ( buf[m++] );
        molecule[i] = static_cast<int> ( buf[m++] );
        v[i][0] = buf[m++];
        v[i][1] = buf[m++];
        v[i][2] = buf[m++];
        image[i] = *( ( tagint * ) &buf[m++] );                             // Ansel
    }

    if ( atom->nextra_border )
        for ( int iextra = 0; iextra < atom->nextra_border; iextra++ )
            m += modify->fix[atom->extra_border[iextra]]->
                 unpack_border( n, first, &buf[m] );
}

/* ---------------------------------------------------------------------- */

int AtomVecRBC::unpack_border_hybrid( int n, int first, double * buf ) {
    int i, m, last;

    m = 0;
    last = first + n;
    for ( i = first; i < last; i++ ) {
        molecule[i] = static_cast<int> ( buf[m++] );
    }
    return m;
}

/* ----------------------------------------------------------------------
   pack data for atom I for sending to another proc
   xyz must be 1st 3 values, so comm::exchange() can test on them
------------------------------------------------------------------------- */

int AtomVecRBC::pack_exchange( int i, double * buf ) {
    int k;

    int m = 1;
    buf[m++] = x[i][0];
    buf[m++] = x[i][1];
    buf[m++] = x[i][2];
    buf[m++] = v[i][0];
    buf[m++] = v[i][1];
    buf[m++] = v[i][2];
    buf[m++] = tag[i];
    buf[m++] = type[i];
    buf[m++] = mask[i];
    buf[m] = 0.0;      // for valgrind
    *( ( tagint * ) &buf[m++] ) = image[i];
    buf[m++] = molecule[i];

    buf[m++] = num_bond[i];
    for ( k = 0; k < num_bond[i]; k++ ) {
        buf[m++] = bond_type[i][k];
        buf[m++] = bond_atom[i][k];
        buf[m++] = bond_r0[i][k];
    }

    buf[m++] = num_angle[i];
    for ( k = 0; k < num_angle[i]; k++ ) {
        buf[m++] = angle_type[i][k];
        buf[m++] = angle_atom1[i][k];
        buf[m++] = angle_atom2[i][k];
        buf[m++] = angle_atom3[i][k];
        buf[m++] = angle_a0[i][k];
    }

    buf[m++] = num_dihedral[i];
    for ( k = 0; k < num_dihedral[i]; k++ ) {
        buf[m++] = dihedral_type[i][k];
        buf[m++] = dihedral_atom1[i][k];
        buf[m++] = dihedral_atom2[i][k];
        buf[m++] = dihedral_atom3[i][k];
        buf[m++] = dihedral_atom4[i][k];
    }

    buf[m++] = nspecial[i][0];
    buf[m++] = nspecial[i][1];
    buf[m++] = nspecial[i][2];
    for ( k = 0; k < nspecial[i][2]; k++ ) buf[m++] = special[i][k];

    if ( atom->nextra_grow )
        for ( int iextra = 0; iextra < atom->nextra_grow; iextra++ )
            m += modify->fix[atom->extra_grow[iextra]]->pack_exchange( i, &buf[m] );

    buf[0] = m;
    return m;
}

/* ---------------------------------------------------------------------- */

int AtomVecRBC::unpack_exchange( double * buf ) {
    int k;

    int nlocal = atom->nlocal;
    if ( nlocal == nmax ) grow( 0 );

    int m = 1;
    x[nlocal][0] = buf[m++];
    x[nlocal][1] = buf[m++];
    x[nlocal][2] = buf[m++];
    v[nlocal][0] = buf[m++];
    v[nlocal][1] = buf[m++];
    v[nlocal][2] = buf[m++];
    tag[nlocal] = static_cast<int> ( buf[m++] );
    type[nlocal] = static_cast<int> ( buf[m++] );
    mask[nlocal] = static_cast<int> ( buf[m++] );
    image[nlocal] = *( ( tagint * ) &buf[m++] );
    molecule[nlocal] = static_cast<int> ( buf[m++] );

    num_bond[nlocal] = static_cast<int> ( buf[m++] );
    for ( k = 0; k < num_bond[nlocal]; k++ ) {
        bond_type[nlocal][k] = static_cast<int> ( buf[m++] );
        bond_atom[nlocal][k] = static_cast<int> ( buf[m++] );
        bond_r0[nlocal][k] = buf[m++];
    }

    num_angle[nlocal] = static_cast<int> ( buf[m++] );
    for ( k = 0; k < num_angle[nlocal]; k++ ) {
        angle_type[nlocal][k] = static_cast<int> ( buf[m++] );
        angle_atom1[nlocal][k] = static_cast<int> ( buf[m++] );
        angle_atom2[nlocal][k] = static_cast<int> ( buf[m++] );
        angle_atom3[nlocal][k] = static_cast<int> ( buf[m++] );
        angle_a0[nlocal][k] = buf[m++];
    }

    num_dihedral[nlocal] = static_cast<int> ( buf[m++] );
    for ( k = 0; k < num_dihedral[nlocal]; k++ ) {
        dihedral_type[nlocal][k] = static_cast<int> ( buf[m++] );
        dihedral_atom1[nlocal][k] = static_cast<int> ( buf[m++] );
        dihedral_atom2[nlocal][k] = static_cast<int> ( buf[m++] );
        dihedral_atom3[nlocal][k] = static_cast<int> ( buf[m++] );
        dihedral_atom4[nlocal][k] = static_cast<int> ( buf[m++] );
    }

    nspecial[nlocal][0] = static_cast<int> ( buf[m++] );
    nspecial[nlocal][1] = static_cast<int> ( buf[m++] );
    nspecial[nlocal][2] = static_cast<int> ( buf[m++] );
    for ( k = 0; k < nspecial[nlocal][2]; k++ )
        special[nlocal][k] = static_cast<int> ( buf[m++] );

    if ( atom->nextra_grow )
        for ( int iextra = 0; iextra < atom->nextra_grow; iextra++ )
            m += modify->fix[atom->extra_grow[iextra]]->
                 unpack_exchange( nlocal, &buf[m] );

    atom->nlocal++;
    return m;
}

/* ----------------------------------------------------------------------
   size of restart data for all atoms owned by this proc
   include extra data stored by fixes
------------------------------------------------------------------------- */

int AtomVecRBC::size_restart() {
    int i;

    int nlocal = atom->nlocal;
    int n = 0;
    for ( i = 0; i < nlocal; i++ )
        n += 15 + 2 * num_bond[i] + 5 * num_angle[i] + 5 * num_dihedral[i];

    if ( atom->nextra_restart )
        for ( int iextra = 0; iextra < atom->nextra_restart; iextra++ )
            for ( i = 0; i < nlocal; i++ )
                n += modify->fix[atom->extra_restart[iextra]]->size_restart( i );

    return n;
}

/* ----------------------------------------------------------------------
   pack atom I's data for restart file including extra quantities
   xyz must be 1st 3 values, so that read_restart can test on them
   molecular types may be negative, but write as positive
------------------------------------------------------------------------- */

int AtomVecRBC::pack_restart( int i, double * buf ) {
    int k;

    int m = 1;
    buf[m++] = x[i][0];
    buf[m++] = x[i][1];
    buf[m++] = x[i][2];
    buf[m++] = tag[i];
    buf[m++] = type[i];
    buf[m++] = mask[i];
    *( ( tagint * ) &buf[m++] ) = image[i];
    buf[m++] = v[i][0];
    buf[m++] = v[i][1];
    buf[m++] = v[i][2];

    buf[m++] = molecule[i];

    buf[m++] = num_bond[i];
    for ( k = 0; k < num_bond[i]; k++ ) {
        buf[m++] = MAX( bond_type[i][k], -bond_type[i][k] );
        buf[m++] = bond_atom[i][k];
        buf[m++] = bond_r0[i][k];
    }

    buf[m++] = num_angle[i];
    for ( k = 0; k < num_angle[i]; k++ ) {
        buf[m++] = MAX( angle_type[i][k], -angle_type[i][k] );
        buf[m++] = angle_atom1[i][k];
        buf[m++] = angle_atom2[i][k];
        buf[m++] = angle_atom3[i][k];
        buf[m++] = angle_a0[i][k];
    }

    buf[m++] = num_dihedral[i];
    for ( k = 0; k < num_dihedral[i]; k++ ) {
        buf[m++] = MAX( dihedral_type[i][k], -dihedral_type[i][k] );
        buf[m++] = dihedral_atom1[i][k];
        buf[m++] = dihedral_atom2[i][k];
        buf[m++] = dihedral_atom3[i][k];
        buf[m++] = dihedral_atom4[i][k];
    }

    if ( atom->nextra_restart )
        for ( int iextra = 0; iextra < atom->nextra_restart; iextra++ )
            m += modify->fix[atom->extra_restart[iextra]]->pack_restart( i, &buf[m] );

    buf[0] = m;
    return m;
}

/* ----------------------------------------------------------------------
   unpack data for one atom from restart file including extra quantities
------------------------------------------------------------------------- */

int AtomVecRBC::unpack_restart( double * buf ) {
    int k;

    int nlocal = atom->nlocal;
    if ( nlocal == nmax ) {
        grow( 0 );
        if ( atom->nextra_store )
            memory->grow( atom->extra, nmax, atom->nextra_store, "atom:extra" );
    }

    int m = 1;
    x[nlocal][0] = buf[m++];
    x[nlocal][1] = buf[m++];
    x[nlocal][2] = buf[m++];
    tag[nlocal] = static_cast<int> ( buf[m++] );
    type[nlocal] = static_cast<int> ( buf[m++] );
    mask[nlocal] = static_cast<int> ( buf[m++] );
    image[nlocal] = *( ( tagint * ) &buf[m++] );
    v[nlocal][0] = buf[m++];
    v[nlocal][1] = buf[m++];
    v[nlocal][2] = buf[m++];
    molecule[nlocal] = static_cast<int> ( buf[m++] );

    num_bond[nlocal] = static_cast<int> ( buf[m++] );
    for ( k = 0; k < num_bond[nlocal]; k++ ) {
        bond_type[nlocal][k] = static_cast<int> ( buf[m++] );
        bond_atom[nlocal][k] = static_cast<int> ( buf[m++] );
        bond_r0[nlocal][k] = buf[m++];
    }

    num_angle[nlocal] = static_cast<int> ( buf[m++] );
    for ( k = 0; k < num_angle[nlocal]; k++ ) {
        angle_type[nlocal][k] = static_cast<int> ( buf[m++] );
        angle_atom1[nlocal][k] = static_cast<int> ( buf[m++] );
        angle_atom2[nlocal][k] = static_cast<int> ( buf[m++] );
        angle_atom3[nlocal][k] = static_cast<int> ( buf[m++] );
        angle_a0[nlocal][k] = buf[m++];
    }

    num_dihedral[nlocal] = static_cast<int> ( buf[m++] );
    for ( k = 0; k < num_dihedral[nlocal]; k++ ) {
        dihedral_type[nlocal][k] = static_cast<int> ( buf[m++] );
        dihedral_atom1[nlocal][k] = static_cast<int> ( buf[m++] );
        dihedral_atom2[nlocal][k] = static_cast<int> ( buf[m++] );
        dihedral_atom3[nlocal][k] = static_cast<int> ( buf[m++] );
        dihedral_atom4[nlocal][k] = static_cast<int> ( buf[m++] );
    }

    nspecial[nlocal][0] = nspecial[nlocal][1] = nspecial[nlocal][2] = 0;

    double ** extra = atom->extra;
    if ( atom->nextra_store ) {
        int size = static_cast<int> ( buf[0] ) - m;
        for ( int i = 0; i < size; i++ ) extra[nlocal][i] = buf[m++];
    }

    atom->nlocal++;
    return m;
}

/* ----------------------------------------------------------------------
   create one atom of itype at coord
   set other values to defaults
------------------------------------------------------------------------- */

void AtomVecRBC::create_atom( int itype, double * coord ) {
    int nlocal = atom->nlocal;
    if ( nlocal == nmax ) grow( 0 );

    tag[nlocal] = 0;
    type[nlocal] = itype;
    x[nlocal][0] = coord[0];
    x[nlocal][1] = coord[1];
    x[nlocal][2] = coord[2];
    mask[nlocal] = 1;
    image[nlocal] = ( ( tagint ) IMGMAX << IMG2BITS ) |
                    ( ( tagint ) IMGMAX << IMGBITS ) | IMGMAX;
    v[nlocal][0] = 0.0;
    v[nlocal][1] = 0.0;
    v[nlocal][2] = 0.0;

    molecule[nlocal] = 0;
    num_bond[nlocal] = 0;
    num_angle[nlocal] = 0;
    num_dihedral[nlocal] = 0;
    nspecial[nlocal][0] = nspecial[nlocal][1] = nspecial[nlocal][2] = 0;

    atom->nlocal++;
}

/* ----------------------------------------------------------------------
   unpack one line from Atoms section of data file
   initialize other atom quantities
------------------------------------------------------------------------- */

void AtomVecRBC::data_atom( double * coord, tagint imagetmp, char ** values ) {
    int nlocal = atom->nlocal;
    if ( nlocal == nmax ) grow( 0 );

    tag[nlocal] = atoi( values[0] );
    if ( tag[nlocal] <= 0 )
        error->one( FLERR, "Invalid atom ID in Atoms section of data file" );

    molecule[nlocal] = atoi( values[1] );

    type[nlocal] = atoi( values[2] );
    if ( type[nlocal] <= 0 || type[nlocal] > atom->ntypes )
        error->one( FLERR, "Invalid atom type in Atoms section of data file" );

    x[nlocal][0] = coord[0];
    x[nlocal][1] = coord[1];
    x[nlocal][2] = coord[2];

    image[nlocal] = imagetmp;

    mask[nlocal] = 1;
    v[nlocal][0] = 0.0;
    v[nlocal][1] = 0.0;
    v[nlocal][2] = 0.0;
    num_bond[nlocal] = 0;
    num_angle[nlocal] = 0;
    num_dihedral[nlocal] = 0;

    atom->nlocal++;
}

/* ----------------------------------------------------------------------
   unpack hybrid quantities from one line in Atoms section of data file
   initialize other atom quantities for this sub-style
------------------------------------------------------------------------- */

int AtomVecRBC::data_atom_hybrid( int nlocal, char ** values ) {
    molecule[nlocal] = atoi( values[0] );

    num_bond[nlocal] = 0;
    num_angle[nlocal] = 0;
    num_dihedral[nlocal] = 0;

    return 2;
}

/* ----------------------------------------------------------------------
   pack atom info for data file including 3 image flags
------------------------------------------------------------------------- */

void AtomVecRBC::pack_data( double ** buf ) {
    int nlocal = atom->nlocal;
    for ( int i = 0; i < nlocal; i++ ) {
        buf[i][0] = tag[i];
        buf[i][1] = molecule[i];
        buf[i][2] = type[i];
        buf[i][3] = x[i][0];
        buf[i][4] = x[i][1];
        buf[i][5] = x[i][2];
        buf[i][6] = ( image[i] & IMGMASK ) - IMGMAX;
        buf[i][7] = ( image[i] >> IMGBITS & IMGMASK ) - IMGMAX;
        buf[i][8] = ( image[i] >> IMG2BITS ) - IMGMAX;
    }
}

/* ----------------------------------------------------------------------
   pack hybrid atom info for data file
------------------------------------------------------------------------- */

int AtomVecRBC::pack_data_hybrid( int i, double * buf ) {
    buf[0] = molecule[i];
    return 2;
}

/* ----------------------------------------------------------------------
   write atom info to data file including 3 image flags
------------------------------------------------------------------------- */

void AtomVecRBC::write_data( FILE * fp, int n, double ** buf ) {
    for ( int i = 0; i < n; i++ )
        fprintf( fp, "%d %d %d %-1.16e %-1.16e %-1.16e %d %d %d\n",
                 ( int ) buf[i][0], ( int ) buf[i][1], ( int ) buf[i][2],
                 buf[i][3], buf[i][4], buf[i][5],
                 ( int ) buf[i][6], ( int ) buf[i][7], ( int ) buf[i][8] );
}

/* ----------------------------------------------------------------------
   write hybrid atom info to data file
------------------------------------------------------------------------- */

int AtomVecRBC::write_data_hybrid( FILE * fp, double * buf ) {
    fprintf( fp, " %d ", ( int ) buf[0] );
    return 2;
}

/* ----------------------------------------------------------------------
   return # of bytes of allocated memory
------------------------------------------------------------------------- */

bigint AtomVecRBC::memory_usage() {
    bigint bytes = 0;

    if ( atom->memcheck( "tag" ) ) bytes += memory->usage( tag, nmax );
    if ( atom->memcheck( "type" ) ) bytes += memory->usage( type, nmax );
    if ( atom->memcheck( "mask" ) ) bytes += memory->usage( mask, nmax );
    if ( atom->memcheck( "image" ) ) bytes += memory->usage( image, nmax );
    if ( atom->memcheck( "x" ) ) bytes += memory->usage( x, nmax, 3 );
    if ( atom->memcheck( "v" ) ) bytes += memory->usage( v, nmax, 3 );
    if ( atom->memcheck( "f" ) ) bytes += memory->usage( f, nmax * comm->nthreads, 3 );

    if ( atom->memcheck( "molecule" ) ) bytes += memory->usage( molecule, nmax );
    if ( atom->memcheck( "nspecial" ) ) bytes += memory->usage( nspecial, nmax, 3 );
    if ( atom->memcheck( "special" ) )
        bytes += memory->usage( special, nmax, atom->maxspecial );

    if ( atom->memcheck( "num_bond" ) ) bytes += memory->usage( num_bond, nmax );
    if ( atom->memcheck( "bond_type" ) )
        bytes += memory->usage( bond_type, nmax, atom->bond_per_atom );
    if ( atom->memcheck( "bond_atom" ) )
        bytes += memory->usage( bond_atom, nmax, atom->bond_per_atom );
    if ( atom->memcheck( "bond_r0" ) )
        bytes += memory->usage( bond_r0, nmax, atom->bond_per_atom );

    if ( atom->memcheck( "num_angle" ) ) bytes += memory->usage( num_angle, nmax );
    if ( atom->memcheck( "angle_type" ) )
        bytes += memory->usage( angle_type, nmax, atom->angle_per_atom );
    if ( atom->memcheck( "angle_atom1" ) )
        bytes += memory->usage( angle_atom1, nmax, atom->angle_per_atom );
    if ( atom->memcheck( "angle_atom2" ) )
        bytes += memory->usage( angle_atom2, nmax, atom->angle_per_atom );
    if ( atom->memcheck( "angle_atom3" ) )
        bytes += memory->usage( angle_atom3, nmax, atom->angle_per_atom );
    if ( atom->memcheck( "angle_a0" ) )
        bytes += memory->usage( angle_a0, nmax, atom->angle_per_atom );

    if ( atom->memcheck( "num_dihedral" ) ) bytes += memory->usage( num_dihedral, nmax );
    if ( atom->memcheck( "dihedral_type" ) )
        bytes += memory->usage( dihedral_type, nmax, atom->dihedral_per_atom );
    if ( atom->memcheck( "dihedral_atom1" ) )
        bytes += memory->usage( dihedral_atom1, nmax, atom->dihedral_per_atom );
    if ( atom->memcheck( "dihedral_atom2" ) )
        bytes += memory->usage( dihedral_atom2, nmax, atom->dihedral_per_atom );
    if ( atom->memcheck( "dihedral_atom3" ) )
        bytes += memory->usage( dihedral_atom3, nmax, atom->dihedral_per_atom );
    if ( atom->memcheck( "dihedral_atom4" ) )
        bytes += memory->usage( dihedral_atom4, nmax, atom->dihedral_per_atom );

    return bytes;
}
