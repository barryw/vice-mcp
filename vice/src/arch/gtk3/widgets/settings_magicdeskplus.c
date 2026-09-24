/** \file   settings_magicdeskplus.c
 * \brief   Settings widget to control Magic Desk Plus resources
 *
 * Based on the VICE GTK3 settings widgets by
 *  Bas Wassink <b.wassink@ziggo.nl>
 *
 * Adapted for Magic Desk Plus by
 *  Claus Schlereth
 */

/*
 * $VICERES MagicDeskPlusSRAMImage    x64 x64sc xscpu64 x128
 * $VICERES MagicDeskPlusSRAMWrite    x64 x64sc xscpu64 x128
 * $VICERES MagicDeskPlusEEPROMImage  x64 x64sc xscpu64 x128
 * $VICERES MagicDeskPlusEEPROMWrite  x64 x64sc xscpu64 x128
 * $VICERES MagicDeskPlusRevision     x64 x64sc xscpu64 x128
 */

/*
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See README for copyright notice.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *  02111-1307  USA.
 *
 */

#include "vice.h"
#include <gtk/gtk.h>

#include "cartridge.h"
#include "magicdeskplus.h"
#include "resources.h"
#include "vice_gtk3.h"

#include "settings_magicdeskplus.h"

/* CAUTION: must match the order of the revision numbers */
static const vice_gtk3_combo_entry_int_t revisions[] = {
    { "SRAM and 32KiB EEPROM", MAGICDESKPLUS_REV_SRAM_EEPROM_32K },
    { "SRAM and 8KiB EEPROM", MAGICDESKPLUS_REV_SRAM_EEPROM_8K },
    { "32KiB EEPROM", MAGICDESKPLUS_REV_EEPROM_32K },
    { "8KiB EEPROM", MAGICDESKPLUS_REV_EEPROM_8K },
    { "SRAM", MAGICDESKPLUS_REV_SRAM },
    VICE_GTK3_COMBO_ENTRY_INT_LIST_END
};

static int get_active_revision(void)
{
    int revision = MAGICDESKPLUS_REV_SRAM_EEPROM_32K;

    resources_get_int("MagicDeskPlusRevision", &revision);
    return revision;
}

static void on_revision_changed(GtkComboBox *combo, gpointer user_data)
{
    GtkWidget *sram;
    GtkWidget *eeprom;
    int revision;

    sram = g_object_get_data(G_OBJECT(combo), "mdp-sram-widget");
    eeprom = g_object_get_data(G_OBJECT(combo), "mdp-eeprom-widget");
    revision = gtk_combo_box_get_active(combo);
    if (revision < 0 ||
        revision > MAGICDESKPLUS_MAX_REV) {
        return;
    }

    resources_set_int("MagicDeskPlusRevision", revision);

    gtk_widget_set_visible(sram, (revision != MAGICDESKPLUS_REV_EEPROM_32K) && (revision != MAGICDESKPLUS_REV_EEPROM_8K));
    gtk_widget_set_visible(eeprom, revision != MAGICDESKPLUS_REV_SRAM);
}


/** \brief  Create widget to control Magic Desk Plus resources
 *
 * \param[in]   parent  parent widget, used for dialogs
 *
 * \return  GtkGrid
 */
GtkWidget *settings_magicdeskplus_widget_create(GtkWidget *parent)
{
    GtkWidget *grid;
    GtkWidget *label;
    GtkWidget *revision;
    GtkWidget *secondary;
    GtkWidget *tertiary;
    int n;
    int iscrt = cartridge_get_filetype(CARTRIDGE_MAGIC_DESK_PLUS) == CARTRIDGE_FILETYPE_CRT ? 1 : 0;

    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 32);

    label = gtk_label_new("Hardware revision");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_valign(label, GTK_ALIGN_CENTER);

    revision = gtk_combo_box_text_new();
    n = 0; while (revisions[n].name) {
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(revision),
                                   revisions[n].name);
        n++;
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(revision),
                             get_active_revision());
    gtk_widget_set_sensitive(revision, !iscrt);
    gtk_widget_set_tooltip_text(
        revision,
        iscrt
            ? "Hardware revision is defined by the attached CRT image"
            : "Hardware revision used for raw cartridge images");
    gtk_widget_set_halign(revision, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), label, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), revision, 1, 0, 1, 1);

    /* secondary image: 128KB SRAM */
    secondary = cart_image_widget_new(CARTRIDGE_MAGIC_DESK_PLUS,
                                      CARTRIDGE_NAME_MAGIC_DESK_PLUS,
                                      CART_IMAGE_SECONDARY,
                                      "SRAM",
                                      "MagicDeskPlusSRAMImage",
                                      FALSE,
                                      TRUE);
    cart_image_widget_append_filename_check(
        secondary,
        "MagicDeskPlusSRAMWrite",
        "Save SRAM image on detach or exit");
    cart_image_widget_set_save_button_label(secondary,
                                            "Save image now ...");
    gtk_grid_attach(GTK_GRID(grid), secondary, 0, 1, 2, 1);

    /* tertiary image: parallel EEPROM */
    tertiary = cart_image_widget_new(CARTRIDGE_MAGIC_DESK_PLUS,
                                     CARTRIDGE_NAME_MAGIC_DESK_PLUS,
                                     CART_IMAGE_TERTIARY,
                                     "EEPROM",
                                     "MagicDeskPlusEEPROMImage",
                                     FALSE,
                                     TRUE);
    cart_image_widget_append_filename_check(
        tertiary,
        "MagicDeskPlusEEPROMWrite",
        "Save EEPROM image on detach or exit");
    cart_image_widget_set_save_button_label(tertiary,
                                            "Save image now ...");
    gtk_grid_attach(GTK_GRID(grid), tertiary, 0, 2, 2, 1);

    g_object_set_data(G_OBJECT(revision), "mdp-sram-widget", secondary);
    g_object_set_data(G_OBJECT(revision), "mdp-eeprom-widget", tertiary);
    g_signal_connect(revision, "changed", G_CALLBACK(on_revision_changed), NULL);

    gtk_widget_show_all(grid);
    gtk_widget_set_no_show_all(secondary, TRUE);
    gtk_widget_set_no_show_all(tertiary, TRUE);
    on_revision_changed(GTK_COMBO_BOX(revision), NULL);
    return grid;
}
