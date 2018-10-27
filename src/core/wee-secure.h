/*
 * Copyright (C) 2013-2018 Sébastien Helleu <flashcode@flashtux.org>
 *
 * This file is part of WeeChat, the extensible chat client.
 *
 * WeeChat is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * WeeChat is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with WeeChat.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef WEECHAT_SECURE_H
#define WEECHAT_SECURE_H

#define SECURE_ENV_PASSPHRASE       "WEECHAT_PASSPHRASE"
#define SECURE_SALT_DEFAULT         "WeeChat!"
#define SECURE_DATA_PASSPHRASE_FLAG "__passphrase__"
#define SECURE_SALT_SIZE            8

enum t_secure_config_hash_algo
{
    SECURE_CONFIG_HASH_SHA224 = 0,
    SECURE_CONFIG_HASH_SHA256,
    SECURE_CONFIG_HASH_SHA384,
    SECURE_CONFIG_HASH_SHA512,
};

enum t_secure_config_cipher
{
    SECURE_CONFIG_CIPHER_AES128 = 0,
    SECURE_CONFIG_CIPHER_AES192,
    SECURE_CONFIG_CIPHER_AES256,
};

extern char *secure_passphrase;
extern struct t_hashtable *secure_hashtable_data;
extern struct t_hashtable *secure_hashtable_data_encrypted;
extern char *secure_hash_algo_string[];
extern int secure_hash_algo[];
extern char *secure_cipher_string[];
extern int secure_cipher[];
extern int secure_data_encrypted;
extern char *secure_decrypt_error[];

extern int secure_encrypt_data (const char *data, int length_data,
                                int hash_algo, int cipher,
                                const char *passphrase, char **encrypted,
                                int *length_encrypted);
extern int secure_decrypt_data (const char *buffer, int length_buffer,
                                int hash_algo, int cipher,
                                const char *passphrase, char **decrypted,
                                int *length_decrypted);
extern int secure_decrypt_data_not_decrypted (const char *passphrase);
extern int secure_init ();
extern void secure_end ();

#endif /* WEECHAT_SECURE_H */
