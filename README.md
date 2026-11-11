# Pistis — extensões do XRP Ledger

Fork do `rippled` que implementa o suporte do protocolo ao framework Pistis,
com políticas independentes para Depositantes e Tomadores.

## Amendment local

As extensões são controladas pelo amendment `PistisLending`, suportado pelo
fork e desabilitado por padrão (`DefaultNo`). Não corresponde a uma alteração
aprovada nas redes públicas do XRP Ledger.

## Extensões do protocolo

| Transação ou objeto | Alteração |
| --- | --- |
| `VaultCreate`, `VaultSet` e `Vault` | Campo opcional `BorrowerDomainID` para a política do Tomador. |
| `LoanSet` | Campo `VaultID`, obrigatório quando o Vault possui política do Tomador. |
| `Loan` | Registro imutável de `CredentialIssuer` e `CredentialType` utilizados na autorização. |

A política do Depositante mantém o `DomainID` nativo, armazenado na emissão
de cotas do Vault. O comportamento de `VaultDeposit` permanece inalterado.

### Regras de autorização

- `VaultCreate` exige que o domínio do Tomador informado exista e seja não nulo.
- `VaultSet` permite incluir, substituir ou remover `BorrowerDomainID` somente
  sem cotas em circulação e sem objetos `Loan` nos Loan Brokers vinculados.
  Empréstimos quitados ainda contam até a exclusão do objeto. O conteúdo dos
  domínios continua administrável por seus proprietários.
- `LoanSet` verifica a correspondência entre o `VaultID` informado e o Vault do
  Loan Broker. O Tomador é a contraparte da conta proprietária do Loan Broker.
- A autorização ocorre no ledger: basta uma Credential aceita e não expirada
  cujo par emissor/tipo pertença ao Permissioned Domain do Tomador. O primeiro
  par válido na ordem armazenada é registrado no Loan.
- Sem `BorrowerDomainID`, a originação preserva o comportamento nativo.
  `LoanPay` e `LoanDelete` não revalidam credenciais; a revogação não extingue
  empréstimos existentes. Não há consulta a VCs, VPs, DIDs ou serviços externos.

## Adaptações do projeto

- Formatos de transações e objetos, campos serializados, modelos C++ gerados
  e invariantes atualizados para as extensões.
- Testes adicionados para autorização, rejeição, compatibilidade, revogação,
  registro da credencial e restrições de alteração da política.
- Docker independente do repositório do TCC e workflow manual de build/testes,
  com cache e publicação opcional da imagem no GHCR. O Compose consome a imagem
  pronta; o build local permanece opcional.
- Contas, seeds e estado local excluídos do contexto Docker e ignorados pelo Git.

A compilação completa e a validação dos testes do protocolo permanecem pendentes.
Os clientes Python ainda precisam de suporte aos novos campos.
